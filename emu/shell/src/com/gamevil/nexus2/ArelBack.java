package com.gamevil.nexus2;

import android.app.Activity;
import android.os.Build;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

/**
 * Back for target 35 (emu PLAN.md DES-11, TASK-E5.2).
 * The manifest sets enableOnBackInvokedCallback. From API 33, the system then sends no KEYCODE_BACK key
 * event to the window. It calls this callback for the back gesture and the back button. The callback gives
 * the activity a back key down and up, on the same path as a v1 back key (the focused view, then
 * SkeletonLauncher.onKeyDown, which sends the game key -8). Below API 33 the key event arrives as in v1.
 */
public final class ArelBack {
    private ArelBack() {}

    private static boolean busy;

    /** Call from SkeletonLauncher.onCreate. */
    public static void install(final Activity a) {
        if (Build.VERSION.SDK_INT < 33) return;
        a.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
            OnBackInvokedDispatcher.PRIORITY_DEFAULT, new OnBackInvokedCallback() {
                @Override
                public void onBackInvoked() { send(a); }
            });
    }

    static void send(Activity a) {
        if (busy) return;  // never loop if a platform path calls back into the dispatcher
        busy = true;
        try {
            long t = SystemClock.uptimeMillis();
            a.dispatchKeyEvent(new KeyEvent(t, t, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK, 0));
            a.dispatchKeyEvent(new KeyEvent(t, SystemClock.uptimeMillis(), KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK, 0));
        } finally {
            busy = false;
        }
    }
}
