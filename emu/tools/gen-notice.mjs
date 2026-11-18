// Writes docs/NOTICE.md: the license notices of the third-party code in the bridge (PLAN.md REQ-23,
// TASK-E8.3). The license texts come from the checked source tree in work/core-src (build-core extracts
// it from third_party/archives, which archives.sha256 checks). scripts/build copies the file into the
// APK as assets/emu/NOTICE.txt.
// Usage (from emu/): node tools/gen-notice.mjs
import fs from 'node:fs';

const S = 'work/core-src';
const X = `${S}/dynarmic/externals`;
const LIBS = [
  ['dynarmic', 'ARM guest code translation (all 64-bit ABIs)', 'azahar-emu/dynarmic a46601580d55', `${S}/dynarmic/LICENSE.txt`],
  ['fmt', 'text formatting (used by dynarmic)', 'fmtlib/fmt 7b273fbb541c', `${X}/fmt/LICENSE`],
  ['mcl', 'support library of dynarmic', 'azahar-emu/mcl 5fc4beaf3310', `${X}/mcl/LICENSE`],
  ['oaknut', 'arm64 code emitter (arm64-v8a)', 'merryhime/oaknut 94c726ce0338', `${X}/oaknut/LICENSE`],
  ['robin-map', 'hash map (used by dynarmic)', 'Tessil/robin-map 054ec5ad6744', `${X}/robin-map/LICENSE`],
  ['xbyak', 'x86-64 code emitter (x86_64)', 'herumi/xbyak 8c0098b69f95', `${X}/xbyak/COPYRIGHT`],
  ['Zycore', 'support library of Zydis (x86_64)', 'zyantific/zycore-c 0b2432ced088', `${X}/zycore/LICENSE`],
  ['Zydis', 'x86-64 decoder (x86_64)', 'zyantific/zydis bffbb610cfea', `${X}/zydis/LICENSE`],
  ['Boost', 'header-only C++ libraries (used by dynarmic)', 'Boost 1.90.0, azahar-emu/ext-boost 6a85c3100499', `${S}/boost/LICENSE_1_0.txt`],
];

let out = `# Third-party notices

The 64-bit ABIs (\`arm64-v8a\`, \`x86_64\`) of this APK have a bridge library (\`lib/<abi>/libgameDSO.so\`). The bridge runs the original 32-bit game code in an embedded ARM emulator. The bridge contains the third-party code in the list below. Each part keeps its license. The full license texts follow the list.

The \`armeabi-v7a\` ABI has the original game library, with no emulator.

The game code and the game assets are the original files of Arel Wars. Arel Wars is a trademark of its owner. This project is an unofficial fan project. It has no connection with Gamevil or Com2uS.

The bridge also links the LLVM C++ library (\`libc++\`) of the Android NDK as a static library. Its license is Apache 2.0 with the LLVM exception. The LLVM exception removes the notice requirement for compiled code.

| Library | Use | Source |
|---|---|---|
`;
for (const [n, use, src] of LIBS) out += `| ${n} | ${use} | \`${src}\` |\n`;
for (const [n, , , f] of LIBS) {
  const t = fs.readFileSync(f, 'utf8').replace(/\r\n/g, '\n').trimEnd();
  if (t.includes('```')) throw new Error(`${f} has a code fence`);
  out += `\n## ${n}\n\n\`\`\`text\n${t}\n\`\`\`\n`;
}
fs.writeFileSync('docs/NOTICE.md', out);
console.log(`docs/NOTICE.md: ${LIBS.length} libraries, ${out.length} bytes`);
