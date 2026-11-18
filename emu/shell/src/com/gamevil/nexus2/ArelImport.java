package com.gamevil.nexus2;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;

import com.gamevil.nexus2.xml.NexusUtils;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Locale;
import java.util.TreeMap;

/**
 * Save import (emu PLAN.md DES-14, REQ-16, REQ-17).
 * The user picks the folder that holds one v1 save set (Save%d.dat, Sng%d.dat, option.sav) in the
 * system file picker. Each file is checked: the size word of the 0x48-byte header, the frozen device ID
 * and the file checksum, as the game checks them on load. The present save files move to
 * files/arel-backup/import-TIME/ first. The new files go in through files/arel-import-tmp/ and a rename.
 * On a failure, the old files go back. After an import, the process stops, so the game loads the new set
 * at the next start.
 * Debug builds also take a set that adb put in files/arel-import-in/ (extra "staged" true).
 */
public final class ArelImport extends Activity {
    private static final String TAG = "ArelImport";
    private static final int PICK = 1;
    // The 16-byte XOR table of the save cipher: guest .rodata at ELF VA 0x108694 (GOT 0x120d68 - 0x186d4).
    private static final byte[] KEY = {
        (byte) 0x8a, 0x46, (byte) 0x90, 0x72, 0x09, (byte) 0xf6, (byte) 0x89, (byte) 0xe1,
        (byte) 0x81, 0x56, (byte) 0x83, 0x62, (byte) 0x92, 0x49, 0x19, (byte) 0xd1 };
    private static final int OPTION_SIZE = 0x74;

    private boolean finished;
    private String shown;  // the message of the result dialog, kept over a recreation of the activity

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) {
            // A recreation (for example a turn to landscape): never import again, only show the result again.
            String msg = state.getString("msg");
            if (msg != null) showResult(msg);
            return;
        }
        Intent in = getIntent();
        boolean debug = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        if (debug && in.getBooleanExtra("staged", false)) {
            TreeMap<String, byte[]> set = new TreeMap<String, byte[]>();
            String err = readDir(new File(getFilesDir(), "arel-import-in"), set);
            finishImport(err != null ? err : importSet(set));
            return;
        }
        if (Build.VERSION.SDK_INT < 21) {
            finishImport("This Android version has no folder picker. Use adb to copy the save files.");
            return;
        }
        Intent pick = new Intent("android.intent.action.OPEN_DOCUMENT_TREE");
        String initial = in.getStringExtra("initial");
        if (initial != null && Build.VERSION.SDK_INT >= 26) pick.putExtra("android.provider.extra.INITIAL_URI", Uri.parse(initial));
        try {
            startActivityForResult(pick, PICK);
        } catch (Exception e) {
            finishImport("No file picker found: " + e);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request != PICK) return;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            log("cancelled");
            finish();
            return;
        }
        TreeMap<String, byte[]> set = new TreeMap<String, byte[]>();
        String err = readTree(data.getData(), set);
        finishImport(err != null ? err : importSet(set));
    }

    // ---- reading ----

    private String readTree(Uri tree, TreeMap<String, byte[]> set) {
        ContentResolver cr = getContentResolver();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        Cursor c = null;
        try {
            c = cr.query(children, new String[] { "document_id", "_display_name" }, null, null, null);
            if (c == null) return "Cannot read the folder.";
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (!isSave(name)) continue;
                InputStream s = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0)));
                try { set.put(name, readAll(s)); } finally { s.close(); }
            }
        } catch (Exception e) {
            return "Cannot read the folder: " + e;
        } finally {
            if (c != null) c.close();
        }
        return null;
    }

    private static String readDir(File dir, TreeMap<String, byte[]> set) {
        File[] list = dir.listFiles();
        if (list == null) return "No folder " + dir;
        try {
            for (File f : list) {
                if (!isSave(f.getName())) continue;
                InputStream s = new FileInputStream(f);
                try { set.put(f.getName(), readAll(s)); } finally { s.close(); }
            }
        } catch (Exception e) {
            return "Cannot read " + dir + ": " + e;
        }
        return null;
    }

    private static byte[] readAll(InputStream s) throws Exception {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int n;
        while ((n = s.read(b)) > 0) {
            o.write(b, 0, n);
            if (o.size() > 1 << 20) throw new Exception("file too large");
        }
        return o.toByteArray();
    }

    static boolean isSave(String n) {
        return n.equals("option.sav") || n.matches("Save[0-9]+\\.dat") || n.matches("Sng[0-9]+\\.dat");
    }

    // ---- checks ----

    private static int u32(byte[] b, int o) {
        return (b[o] & 0xff) | (b[o + 1] & 0xff) << 8 | (b[o + 2] & 0xff) << 16 | (b[o + 3] & 0xff) << 24;
    }

    /** The guest generator: state * 214013 + 2531011, output ((state & 0x7fffffff) >> 16) % n. */
    private static final class Rng {
        int s;
        Rng(int seed) { s = seed; }
        int next(int n) { s = s * 214013 + 2531011; return ((s & 0x7fffffff) >>> 16) % n; }
    }

    /** Returns null when the file passes the load checks of the game, else the reason. */
    static String checkEncrypted(String name, byte[] f, String deviceId) {
        if (f.length < 0x4c) return name + ": file too short";
        if (u32(f, 0) != f.length) return name + ": the size in the header is not the file size";
        byte[] d = f.clone();
        int ts = u32(d, 4);
        Rng r = new Rng(ts);
        for (int i = 8; i < d.length; i++) d[i] ^= KEY[r.next(16)];
        int idLen = 0;
        while (idLen < 64 && d[8 + idLen] != 0) idLen++;
        String id = new String(d, 8, idLen);
        if (!id.equals(deviceId)) return name + ": the save belongs to another device ID";
        r = new Rng(ts);
        int sum = r.next(0xffffff);
        for (int i = 0; i < d.length - 4; i++) {
            int x = d[i] & 0xff;
            sum = (sum >>> 24) ^ (x ^ (KEY[x & 15] & 0xff)) ^ (sum << 8);
        }
        if (sum != u32(d, d.length - 4)) return name + ": checksum error (the file is damaged)";
        return null;
    }

    private String checkSet(TreeMap<String, byte[]> set) {
        if (!set.containsKey("option.sav")) return "The folder has no option.sav.";
        if (set.get("option.sav").length != OPTION_SIZE) return "option.sav: wrong size";
        String id = NexusUtils.getDeviceID(this);
        int saves = 0;
        for (String n : set.keySet()) {
            if (n.equals("option.sav")) continue;
            String other = n.startsWith("Save") ? "Sng" + n.substring(4, n.length() - 4) + ".dat"
                                                : "Save" + n.substring(3, n.length() - 4) + ".dat";
            if (!set.containsKey(other)) return n + " has no " + other + ".";
            String e = checkEncrypted(n, set.get(n), id);
            if (e != null) return e;
            if (n.startsWith("Save")) saves++;
        }
        if (saves == 0) return "The folder has no Save file.";
        return null;
    }

    // ---- import ----

    private String importSet(TreeMap<String, byte[]> set) {
        String bad = checkSet(set);
        if (bad != null) return "Import stopped. Nothing changed.\n" + bad;
        File files = getFilesDir();
        File tmp = new File(files, "arel-import-tmp");
        String time = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        File bak = new File(new File(files, "arel-backup"), "import-" + time);
        for (int i = 2; bak.exists(); i++) bak = new File(new File(files, "arel-backup"), "import-" + time + "-" + i);
        ArrayList<String> moved = new ArrayList<String>(), placed = new ArrayList<String>();
        try {
            deleteTree(tmp);
            if (!tmp.mkdirs()) throw new Exception("cannot make " + tmp);
            for (String n : set.keySet()) write(new File(tmp, n), set.get(n));
            if (!bak.mkdirs()) throw new Exception("cannot make " + bak);
            File[] present = files.listFiles();
            if (present != null)
                for (File f : present) {
                    if (!f.isFile() || !isSave(f.getName())) continue;
                    if (!f.renameTo(new File(bak, f.getName()))) throw new Exception("cannot move " + f.getName());
                    moved.add(f.getName());
                }
            for (String n : set.keySet()) {
                if (!new File(tmp, n).renameTo(new File(files, n))) throw new Exception("cannot place " + n);
                placed.add(n);
            }
            deleteTree(tmp);
            log("imported " + set.keySet() + ", old files " + moved + " in " + bak);
            return "Import complete: " + set.size() + " files.\nThe old files are in " + bak.getName()
                + ".\nThe game will close now. Start it again to load the imported saves.";
        } catch (Exception e) {
            for (String n : placed) new File(files, n).delete();
            boolean restored = true;
            for (String n : moved) restored &= new File(bak, n).renameTo(new File(files, n));
            deleteTree(tmp);
            log("import failed: " + e + ", restored " + restored);
            return "Import failed: " + e + (restored ? "\nThe old files are back." : "\nThe old files are in " + bak);
        }
    }

    private static void write(File f, byte[] b) throws Exception {
        FileOutputStream o = new FileOutputStream(f);
        try { o.write(b); o.getFD().sync(); } finally { o.close(); }
    }

    private static void deleteTree(File f) {
        File[] l = f.listFiles();
        if (l != null) for (File c : l) deleteTree(c);
        f.delete();
    }

    // ---- result ----

    private void log(String s) { Log.i(TAG, s); }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        if (shown != null) out.putString("msg", shown);
    }

    private void finishImport(String msg) {
        log("result: " + msg.replace('\n', ' '));
        try { write(new File(getFilesDir(), "arel-import-result.txt"), (msg + "\n").getBytes("UTF-8")); } catch (Exception ignored) { }
        showResult(msg);
    }

    private void showResult(String msg) {
        shown = msg;
        final boolean done = msg.startsWith("Import complete");
        new AlertDialog.Builder(this).setTitle("Save import").setMessage(msg).setCancelable(false)
            .setPositiveButton("OK", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface d, int w) { close(done); }
            }).show();
    }

    private void close(boolean kill) {
        if (finished) return;
        finished = true;
        finish();
        // The game may hold the old saves in memory, and its next save would overwrite the import.
        if (kill) android.os.Process.killProcess(android.os.Process.myPid());
    }
}
