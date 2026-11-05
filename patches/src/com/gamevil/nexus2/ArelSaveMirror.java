package com.gamevil.nexus2;

import android.content.Context;
import android.os.FileObserver;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

/**
 * Keeps a copy of the save set in the app's external files folder, so scripts/pull-saves can fetch it
 * from a non-debuggable build (PLAN.md 6.6). Only ever reads the real saves.
 * An empty save folder never replaces the mirror, so clearing app data does not erase the copy.
 */
public final class ArelSaveMirror extends FileObserver implements Runnable {
    private static final int EVENTS = FileObserver.CLOSE_WRITE | FileObserver.MOVED_TO | FileObserver.DELETE;
    private static final long SETTLE_MS = 1000;
    private static ArelSaveMirror instance;

    private final File source;
    private final File root;
    private final Handler handler;

    private ArelSaveMirror(File source, File root, Handler handler) {
        super(source.getPath(), EVENTS);
        this.source = source;
        this.root = root;
        this.handler = handler;
    }

    public static synchronized void start(Context context) {
        if (instance != null) return;
        try {
            File root = context.getExternalFilesDir(null);
            if (root == null) return;
            HandlerThread thread = new HandlerThread("ArelSaveMirror");
            thread.start();
            instance = new ArelSaveMirror(context.getFilesDir(), root, new Handler(thread.getLooper()));
            instance.startWatching();
            instance.handler.post(instance);
        } catch (Throwable t) {
            Log.w("ArelSaveMirror", "start failed", t);
        }
    }

    /** Called from onPause, so a save made just before the app leaves the screen is not held back. */
    public static synchronized void flush() {
        if (instance == null) return;
        instance.handler.removeCallbacks(instance);
        instance.handler.post(instance);
    }

    private static boolean isSave(String name) {
        return name != null && (name.equals("option.sav") || name.matches("(Save|Sng)[0-9]+\\.dat"));
    }

    @Override
    public void onEvent(int event, String name) {
        if (!isSave(name)) return;
        handler.removeCallbacks(this);
        handler.postDelayed(this, SETTLE_MS);
    }

    @Override
    public void run() {
        try {
            sync();
        } catch (Throwable t) {
            Log.w("ArelSaveMirror", "sync failed", t);
        }
    }

    private void sync() throws Exception {
        File[] files = source.listFiles();
        if (files == null) return;
        File tmp = new File(root, "saves.tmp");
        clear(tmp);
        tmp.mkdirs();
        int copied = 0;
        for (File f : files) {
            if (f.isFile() && isSave(f.getName())) {
                copy(f, new File(tmp, f.getName()));
                copied++;
            }
        }
        if (copied == 0) {
            clear(tmp);
            return;
        }
        File saves = new File(root, "saves");
        File old = new File(root, "saves.old");
        clear(old);
        if (saves.exists() && !saves.renameTo(old)) throw new Exception("could not move the old mirror");
        if (!tmp.renameTo(saves)) throw new Exception("could not move the new mirror");
        clear(old);
    }

    private static void clear(File dir) {
        File[] files = dir.listFiles();
        if (files != null) for (File f : files) f.delete();
        dir.delete();
    }

    private static void copy(File from, File to) throws Exception {
        FileInputStream in = new FileInputStream(from);
        try {
            FileOutputStream out = new FileOutputStream(to);
            try {
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }
}
