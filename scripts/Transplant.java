import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/**
 * Copies a zip entry by entry, keeping order, storage method and the raw compressed bytes of
 * every unchanged entry. Replaced entries keep their original storage method. Old signature
 * files in META-INF are dropped.
 *
 * Usage: java Transplant.java original.apk out.apk [entry=path ...]
 */
public class Transplant {
    record Entry(String name, byte[] nameBytes, int flags, int method, int time, int date,
                 long crc, long csize, long usize, long offset) {}

    public static void main(String[] a) throws Exception {
        if (a.length < 2) throw new IllegalArgumentException("usage: original out [entry=path ...]");
        Map<String, Path> replace = new LinkedHashMap<>();
        for (int i = 2; i < a.length; i++) {
            int eq = a[i].indexOf('=');
            if (eq < 1) throw new IllegalArgumentException("bad replacement: " + a[i]);
            replace.put(a[i].substring(0, eq), Path.of(a[i].substring(eq + 1)));
        }
        byte[] z = Files.readAllBytes(Path.of(a[0]));
        List<Entry> entries = readCentralDirectory(z);
        Set<String> seen = new HashSet<>();
        for (Entry e : entries)
            if (!seen.add(e.name)) throw new IllegalStateException("duplicate entry: " + e.name);
        for (String name : replace.keySet())
            if (!seen.contains(name)) throw new IllegalStateException("no such entry to replace: " + name);

        ByteArrayOutputStream out = new ByteArrayOutputStream(z.length);
        ByteArrayOutputStream cd = new ByteArrayOutputStream();
        int count = 0, replaced = 0;
        for (Entry e : entries) {
            if (isOldSignature(e.name)) continue;
            byte[] data;
            long crc = e.crc, usize = e.usize;
            Path r = replace.get(e.name);
            if (r != null) {
                byte[] raw = Files.readAllBytes(r);
                CRC32 c = new CRC32();
                c.update(raw);
                crc = c.getValue();
                usize = raw.length;
                data = e.method == 0 ? raw : deflate(raw);
                replaced++;
            } else {
                int n = u16(z, (int) e.offset + 26), m = u16(z, (int) e.offset + 28);
                int start = (int) e.offset + 30 + n + m;
                data = Arrays.copyOfRange(z, start, start + (int) e.csize);
            }
            int flags = e.flags & ~8; // sizes are written up front, so no data descriptor
            long offset = out.size();
            put32(out, 0x04034b50); put16(out, 20); put16(out, flags); put16(out, e.method);
            put16(out, e.time); put16(out, e.date); put32(out, crc);
            put32(out, data.length); put32(out, usize);
            put16(out, e.nameBytes.length); put16(out, 0);
            out.write(e.nameBytes);
            out.write(data);

            put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, flags); put16(cd, e.method);
            put16(cd, e.time); put16(cd, e.date); put32(cd, crc);
            put32(cd, data.length); put32(cd, usize);
            put16(cd, e.nameBytes.length); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
            put32(cd, 0); put32(cd, offset);
            cd.write(e.nameBytes);
            count++;
        }
        if (replaced != replace.size()) throw new IllegalStateException("not all replacements applied");
        long cdOffset = out.size();
        cd.writeTo(out);
        put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, count); put16(out, count);
        put32(out, cd.size()); put32(out, cdOffset); put16(out, 0);
        Files.write(Path.of(a[1]), out.toByteArray());
        System.out.println("transplant: " + count + " entries, " + replaced + " replaced");
    }

    static boolean isOldSignature(String n) {
        if (!n.startsWith("META-INF/") || n.indexOf('/', 9) >= 0) return false;
        String u = n.toUpperCase(Locale.ROOT);
        return u.equals("META-INF/MANIFEST.MF") || u.endsWith(".SF") || u.endsWith(".RSA")
                || u.endsWith(".DSA") || u.endsWith(".EC");
    }

    static List<Entry> readCentralDirectory(byte[] z) {
        int eocd = -1;
        for (int i = z.length - 22; i >= 0 && i >= z.length - 22 - 65535; i--)
            if (u32(z, i) == 0x06054b50L) { eocd = i; break; }
        if (eocd < 0) throw new IllegalStateException("no end of central directory");
        int total = u16(z, eocd + 10);
        int p = (int) u32(z, eocd + 16);
        List<Entry> list = new ArrayList<>(total);
        for (int i = 0; i < total; i++) {
            if (u32(z, p) != 0x02014b50L) throw new IllegalStateException("bad central entry " + i);
            int n = u16(z, p + 28), m = u16(z, p + 30), k = u16(z, p + 32);
            byte[] nb = Arrays.copyOfRange(z, p + 46, p + 46 + n);
            list.add(new Entry(new String(nb, java.nio.charset.StandardCharsets.UTF_8), nb,
                    u16(z, p + 8), u16(z, p + 10), u16(z, p + 12), u16(z, p + 14),
                    u32(z, p + 16), u32(z, p + 20), u32(z, p + 24), u32(z, p + 42)));
            p += 46 + n + m + k;
        }
        return list;
    }

    static byte[] deflate(byte[] raw) {
        Deflater d = new Deflater(Deflater.BEST_COMPRESSION, true);
        d.setInput(raw);
        d.finish();
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        while (!d.finished()) o.write(buf, 0, d.deflate(buf));
        d.end();
        return o.toByteArray();
    }

    static int u16(byte[] b, int p) { return (b[p] & 255) | (b[p + 1] & 255) << 8; }
    static long u32(byte[] b, int p) { return (u16(b, p) | (long) u16(b, p + 2) << 16) & 0xffffffffL; }
    static void put16(OutputStream o, int v) throws IOException { o.write(v); o.write(v >> 8); }
    static void put32(OutputStream o, long v) throws IOException {
        put16(o, (int) (v & 0xffff)); put16(o, (int) (v >>> 16));
    }
}
