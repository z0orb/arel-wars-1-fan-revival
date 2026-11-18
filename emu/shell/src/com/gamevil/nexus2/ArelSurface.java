package com.gamevil.nexus2;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;

/**
 * E6: on an API 28 AVD, after a screen lock and unlock, the game SurfaceView has no surface
 * (mRequestedVisible true, mVisible false, no SurfaceControl: its updateSurface did not run again).
 * The v1 GL thread (NexusGLThread) then waits for ever for surfaceCreated, and the screen stays black.
 * The window of the activity has no surface on the app side either (ViewRootImpl.mSurface is not valid), and
 * nothing makes ViewRootImpl call relayoutWindow again. HOME and back recovers it. This class checks the
 * surface 500 ms after each resume of the game activity. If the view is shown but its surface is not valid,
 * it sets the same window attributes again, which makes a relayout. If 1500 ms after the resume the surface is
 * still not valid, it hides the view and shows it again after 100 ms. When the surface is valid, it does nothing.
 * emu/docs/DIVERGENCES.md row 13.
 */
public final class ArelSurface implements Application.ActivityLifecycleCallbacks {
    private static final String TAG = "ArelSurface";
    private static final int SURFACE_VIEW_ID = 0x7f080016; // app:id/SurfaceView01

    private final Activity game;

    private ArelSurface(Activity game) { this.game = game; }

    /** Call after setContentView. */
    public static void install(Activity a) {
        try {
            a.getApplication().registerActivityLifecycleCallbacks(new ArelSurface(a));
        } catch (RuntimeException e) {
            Log.w(TAG, "not installed: " + e);
        }
    }

    @Override
    public void onActivityResumed(Activity a) {
        if (a != game) return;
        View v = a.findViewById(SURFACE_VIEW_ID);
        if (!(v instanceof SurfaceView)) return;
        final SurfaceView sv = (SurfaceView) v;
        sv.postDelayed(new Runnable() {
            @Override
            public void run() { check(game, sv, false); }
        }, 500);
        sv.postDelayed(new Runnable() {
            @Override
            public void run() { check(game, sv, true); }
        }, 1500);
    }

    @Override
    public void onActivityDestroyed(Activity a) {
        if (a == game) a.getApplication().unregisterActivityLifecycleCallbacks(this);
    }

    static void check(Activity a, final SurfaceView sv, boolean second) {
        if (!sv.isAttachedToWindow() || sv.getVisibility() != View.VISIBLE || sv.getWindowVisibility() != View.VISIBLE) return;
        if (sv.getHolder().getSurface().isValid()) return;
        if (!second) {
            // The window of the activity also has no surface on the app side (ViewRootImpl.mSurface is not
            // valid). The same attributes again make ViewRootImpl call relayoutWindow, which gives a surface.
            Log.i(TAG, "the game view has no surface after the resume: relayout the window");
            a.getWindow().setAttributes(a.getWindow().getAttributes());
            return;
        }
        Log.i(TAG, "the game view has no surface after the relayout: hide and show it");
        sv.setVisibility(View.INVISIBLE);
        sv.postDelayed(new Runnable() {
            @Override
            public void run() { sv.setVisibility(View.VISIBLE); }
        }, 100);
    }

    @Override public void onActivityCreated(Activity a, Bundle b) {}
    @Override public void onActivityStarted(Activity a) {}
    @Override public void onActivityPaused(Activity a) {}
    @Override public void onActivityStopped(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
}
