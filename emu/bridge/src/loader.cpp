// ELF loader for the 32-bit ARM guest (DES-05), from universal/oracle/src/loader.c.
// Copies the PT_LOAD segments into guest memory at SO_BASE, zeroes .bss, applies .rel.dyn and
// .rel.plt, and resolves each import to its svc thunk (or a guest data word).
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include "guest.h"

namespace emu {

void mem_commit_image(uint32_t base, uint32_t size);

#define PAGE 0x1000u
#define PUP(x) (((x) + PAGE - 1) & ~(PAGE - 1))

static uint8_t *file;
static uint32_t file_size, load_end, text_end;
static uint32_t dynsym, dynstr, ndynsym;           // guest addresses
static Elf32_Sym *symtab; static const char *strtab; static uint32_t nsymtab;  // host copy
static uint32_t init_array, init_array_sz;

static void apply_rel(uint32_t rel, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = R32(rel + 8 * i), info = R32(rel + 8 * i + 4);
        uint32_t type = ELF32_R_TYPE(info), si = ELF32_R_SYM(info);
        uint32_t where = VA(off);
        switch (type) {
        case R_ARM_NONE: break;
        case R_ARM_RELATIVE: W32(where, R32(where) + SO_BASE); break;
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
        case R_ARM_ABS32: {
            Elf32_Sym s; memcpy(&s, P(dynsym + 16 * si, 16), 16);
            const char *name = S(dynstr + s.st_name);
            uint32_t v;
            if (s.st_shndx != SHN_UNDEF) v = VA(s.st_value);
            else if ((v = shim_data(name)) != 0) {}
            else {
                int k = shim_index(name);
                if (k < 0) die("loader: no shim for import %s", name);
                v = thunk(SVC_IMPORT + (uint32_t)k);
            }
            if (type == R_ARM_ABS32) W32(where, R32(where) + v); else W32(where, v);
            break;
        }
        default: die("loader: relocation type %u at 0x%x not handled", type, off);
        }
    }
}

static void so_load_file(const so_patch_t *patches, int npatches, const char *path);

void so_load(const char *path, const so_patch_t *patches, int npatches) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("loader: open %s failed", path);
    struct stat st; fstat(fd, &st);
    file_size = (uint32_t)st.st_size;
    file = (uint8_t *)malloc(file_size);
    if (pread(fd, file, file_size, 0) != (ssize_t)file_size) die("loader: read failed");
    close(fd);
    so_load_file(patches, npatches, path);
}

// The bridge: the guest is an embedded blob (DES-03). The loader works on its own copy.
void so_load_mem(const uint8_t *data, uint32_t size, const so_patch_t *patches, int npatches) {
    file_size = size;
    file = (uint8_t *)malloc(file_size);
    memcpy(file, data, size);
    so_load_file(patches, npatches, "embedded guest");
}

static void so_load_file(const so_patch_t *patches, int npatches, const char *path) {
    for (int i = 0; i < npatches; i++) {
        uint16_t w; memcpy(&w, file + patches[i].off, 2);
        if (w != patches[i].before) die("loader: patch at 0x%x expects 0x%04x, found 0x%04x", patches[i].off, patches[i].before, w);
        memcpy(file + patches[i].off, &patches[i].after, 2);
        olog("oracle patch at file offset 0x%x: 0x%04x -> 0x%04x (%s)", patches[i].off, patches[i].before, patches[i].after, patches[i].why);
    }
    Elf32_Ehdr *eh = (Elf32_Ehdr *)file;
    if (memcmp(eh->e_ident, ELFMAG, 4) || eh->e_machine != EM_ARM || eh->e_ident[EI_CLASS] != ELFCLASS32)
        die("loader: %s is not a 32-bit ARM ELF", path);
    Elf32_Phdr *ph = (Elf32_Phdr *)(file + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr + ph[i].p_memsz > load_end) load_end = ph[i].p_vaddr + ph[i].p_memsz;
    load_end = PUP(load_end);
    mem_commit_image(SO_BASE, load_end);
    uint32_t dyn = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        Elf32_Phdr *p = &ph[i];
        if (p->p_type == PT_DYNAMIC) dyn = VA(p->p_vaddr);
        if (p->p_type != PT_LOAD) continue;
        if (p->p_offset + p->p_filesz > file_size) die("loader: segment past the end of the file");
        memcpy(P(VA(p->p_vaddr), p->p_filesz), file + p->p_offset, p->p_filesz);
        if (p->p_flags & PF_X) text_end = p->p_vaddr + p->p_filesz;
        // .bss is already zero: the block was committed fresh.
    }
    if (!dyn) die("loader: no PT_DYNAMIC");
    uint32_t rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0, hash = 0;
    for (uint32_t d = dyn; R32(d) != DT_NULL; d += 8) {
        uint32_t tag = R32(d), v = R32(d + 4);
        switch (tag) {
        case DT_SYMTAB: dynsym = VA(v); break;
        case DT_STRTAB: dynstr = VA(v); break;
        case DT_HASH: hash = v; break;
        case DT_REL: rel = v; break;
        case DT_RELSZ: relsz = v; break;
        case DT_JMPREL: jmprel = v; break;
        case DT_PLTRELSZ: pltrelsz = v; break;
        case DT_INIT_ARRAY: init_array = v; break;
        case DT_INIT_ARRAYSZ: init_array_sz = v; break;
        case DT_TEXTREL: die("loader: DT_TEXTREL present; use the patched v1 library");
        }
    }
    ndynsym = R32(VA(hash) + 4);
    Elf32_Shdr *sh = (Elf32_Shdr *)(file + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; i++)
        if (sh[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf32_Sym *)(file + sh[i].sh_offset);
            nsymtab = sh[i].sh_size / sizeof(Elf32_Sym);
            strtab = (const char *)(file + sh[sh[i].sh_link].sh_offset);
        }
    apply_rel(VA(rel), relsz / 8);
    apply_rel(VA(jmprel), pltrelsz / 8);
}

uint32_t so_sym(const char *name) {
    for (uint32_t i = 1; i < ndynsym; i++) {
        Elf32_Sym s; memcpy(&s, H(dynsym + 16 * i), 16);
        if (s.st_shndx != SHN_UNDEF && s.st_value && !strcmp((const char *)H(dynstr + s.st_name), name)) return VA(s.st_value);
    }
    for (uint32_t i = 1; i < nsymtab; i++)
        if (symtab[i].st_shndx != SHN_UNDEF && symtab[i].st_value && !strcmp(strtab + symtab[i].st_name, name)) return VA(symtab[i].st_value);
    return 0;
}

const char *so_name_at(uint32_t addr, uint32_t *off) {
    const char *best = nullptr; uint32_t bv = 0;
    if (addr < SO_BASE || addr >= SO_BASE + text_end) return nullptr;
    uint32_t va = (addr - SO_BASE) & ~1u;
    for (uint32_t i = 1; i < nsymtab; i++) {
        if (ELF32_ST_TYPE(symtab[i].st_info) != STT_FUNC) continue;
        uint32_t v = symtab[i].st_value & ~1u;
        if (v <= va && v >= bv) { bv = v; best = strtab + symtab[i].st_name; }
    }
    if (off) *off = va - bv;
    return best;
}

void so_run_init() {
    for (uint32_t i = 0; i < init_array_sz / 4; i++) {
        uint32_t a = R32(VA(init_array) + 4 * i);
        if (a == 0 || a == 0xffffffffu) continue;
        olog("init_array[%u] -> 0x%x", i, a);
        gcall0(a);
    }
}

uint32_t so_text_end() { return SO_BASE + text_end; }
uint32_t so_end() { return SO_BASE + load_end; }

}  // namespace emu
