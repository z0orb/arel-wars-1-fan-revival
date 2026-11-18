package com.gamevil.nexus2;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.view.View;

import java.lang.reflect.Method;

/**
 * Sound calls of the guest on a worker thread (emu PLAN.md Phase E7, TASK-E7.4, DIVERGENCES.md 15).
 * The guest calls OnSoundPlay and OnStopSound of eruelwarsUIControllerView on the GL thread, inside
 * NativeRender. v1 then releases the old MediaPlayer and makes a new one (MediaPlayer.create: blocking
 * binder calls to the media server) on the GL thread. In the heaviest battle this made frames of 100 ms
 * and more, also on the native armeabi-v7a path (E7 bench). The smali patch 0005-sound sends both calls
 * here. One HandlerThread runs them in the same order, with the same v1 code (NexusSound, by reflection,
 * because shell/src compiles with no game classes).
 * Pause: when the game activity pauses, the queued calls are dropped, so no sound starts in the background.
 * A call that already runs can still start one sound after the pause. v1 has the same race between the
 * GL thread and the UI thread.
 * If the worker cannot start, each call runs on the calling thread, as in v1.
 */
public final class ArelSound implements Application.ActivityLifecycleCallbacks {
    static final String TAG = "ArelSound";
    private static Handler handler;
    private static boolean tried;
    private static volatile boolean paused;
    private static Method stopBgm, setVolume, playSound, stopAll;
    // The class, not the instance: the system can make the game activity again (a new instance).
    private final Class<?> game;

    private ArelSound(Class<?> game) { this.game = game; }

    private static synchronized void init(View v) {
        if (tried) return;
        tried = true;
        try {
            Class<?> s = Class.forName("com.gamevil.nexus2.ui.NexusSound");
            stopBgm = s.getMethod("stopBGMSound");
            setVolume = s.getMethod("setVolume", int.class);
            playSound = s.getMethod("playSound", int.class, boolean.class);
            stopAll = s.getMethod("stopAllSound");
        } catch (Exception e) {
            Log.w(TAG, "NexusSound not found: " + e);
            return;
        }
        try {
            Context c = v.getContext();
            if (c instanceof Activity) {
                Activity a = (Activity) c;
                a.getApplication().registerActivityLifecycleCallbacks(new ArelSound(a.getClass()));
            }
            HandlerThread t = new HandlerThread(TAG);
            t.start();
            handler = new Handler(t.getLooper());
            Log.i(TAG, "worker started");
        } catch (Exception e) {
            handler = null;
            Log.w(TAG, "no worker, sound calls stay on the calling thread: " + e);
        }
    }

    /** v1 eruelwarsUIControllerView.OnSoundPlay(sndID, vol, isLoop). */
    static void playNow(int id, int vol, boolean loop) {
        try {
            if (vol == 0 && loop) {
                stopBgm.invoke(null);
            } else {
                setVolume.invoke(null, vol / 10);
                playSound.invoke(null, 0x7f060000 + id, loop);
            }
        } catch (Exception e) {
            Log.w(TAG, "play " + id + ": " + e);
        }
    }

    static void stopNow() {
        try {
            stopAll.invoke(null);
        } catch (Exception e) {
            Log.w(TAG, "stop: " + e);
        }
    }

    public static void play(View v, final int id, final int vol, final boolean loop) {
        init(v);
        if (playSound == null) return;
        Handler h = handler;
        if (h == null) { playNow(id, vol, loop); return; }
        h.post(new Runnable() {
            @Override public void run() { if (!paused) playNow(id, vol, loop); }
        });
    }

    public static void stop(View v) {
        init(v);
        if (stopAll == null) return;
        Handler h = handler;
        if (h == null) { stopNow(); return; }
        h.removeCallbacksAndMessages(null);
        h.post(new Runnable() {
            @Override public void run() { stopNow(); }
        });
    }

    @Override public void onActivityPaused(Activity a) {
        if (a.getClass() != game) return;
        paused = true;
        Handler h = handler;
        if (h != null) h.removeCallbacksAndMessages(null);
    }

    @Override public void onActivityResumed(Activity a) {
        if (a.getClass() == game) paused = false;
    }

    @Override public void onActivityCreated(Activity a, Bundle b) {}
    @Override public void onActivityStarted(Activity a) {}
    @Override public void onActivityStopped(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    @Override public void onActivityDestroyed(Activity a) {}
}
