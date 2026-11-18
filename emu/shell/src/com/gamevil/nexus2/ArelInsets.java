package com.gamevil.nexus2;

import android.app.Activity;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * Edge-to-edge and immersive mode for target 35 (emu PLAN.md DES-11, TASK-E5.2).
 * The system bars are hidden (a swipe shows them for a short time). The game root layout (id 0x7f080015)
 * gets the largest 3:2 box inside the safe area: the window minus the visible system bars and the display
 * cutout. The box is centred in the safe area. Its parent is black, as in v1 patch 0003-aspect.
 * The game views fill the root layout, so touch follows the box.
 * Difference from v1 (DIVERGENCES.md): v1 changed the box only on screens wider than 3:2 and stretched the
 * game on narrower screens. This class letterboxes on every screen.
 */
public final class ArelInsets {
    static final int ROOT_ID = 0x7f080015;

    private ArelInsets() {}

    /** Call after setContentView (SkeletonLauncher.onCreate). */
    public static void install(final Activity a) {
        final Window w = a.getWindow();
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }
        final View root = a.findViewById(ROOT_ID);
        if (root == null || !(root.getParent() instanceof View)) return;
        final View parent = (View) root.getParent();
        parent.setBackgroundColor(0xff000000);
        hideBars(a);
        if (Build.VERSION.SDK_INT >= 18) {
            // The system shows the bars again after a dialog, a lock or a swipe. Hide them at each focus gain.
            parent.getViewTreeObserver().addOnWindowFocusChangeListener(
                new android.view.ViewTreeObserver.OnWindowFocusChangeListener() {
                    @Override
                    public void onWindowFocusChanged(boolean has) { if (has) hideBars(a); }
                });
        }
        if (Build.VERSION.SDK_INT >= 20) {
            parent.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
                @Override
                public WindowInsets onApplyWindowInsets(View v, WindowInsets in) {
                    int[] s = safe(in);
                    fit(root, v.getWidth(), v.getHeight(), s);
                    return in;
                }
            });
            parent.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
                @Override
                public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                    if (r - l != or - ol || b - t != ob - ot) v.requestApplyInsets();
                }
            });
            parent.requestApplyInsets();
        }
        if (Build.VERSION.SDK_INT == 26) {
            View sv = a.findViewById(SURFACE_VIEW_ID);
            if (sv instanceof android.view.SurfaceView) Api26.install((android.view.SurfaceView) sv);
        }
    }

    static final int SURFACE_VIEW_ID = 0x7f080016; // app:id/SurfaceView01

    /**
     * E6, API 26 only (Android 8.0): when only the position of a SurfaceView changes, SurfaceView.updateSurface
     * calls mScreenRect.set(left, top, width, height), with the width and height in place of right and bottom.
     * Android 8.1 fixed it (mWindowSpaceLeft + width). The game box starts at x > 0, so the game surface was shown
     * in x 327 to 1620, squeezed. This class runs after the pre-draw listener of the SurfaceView. If mScreenRect
     * is not the box of the view, it sets the right box and gives it to setParentSpaceRectangle again.
     * API 26 has no hidden API restriction. If reflection fails, it writes one log line and does nothing.
     * emu/docs/DIVERGENCES.md row 14.
     */
    static final class Api26 implements android.view.ViewTreeObserver.OnPreDrawListener, View.OnAttachStateChangeListener {
        private final android.view.SurfaceView sv;
        private java.lang.reflect.Field rect, left, top;
        private java.lang.reflect.Method apply;
        private boolean dead;

        private Api26(android.view.SurfaceView sv) { this.sv = sv; }

        static void install(android.view.SurfaceView sv) {
            Api26 f = new Api26(sv);
            try {
                Class<?> c = android.view.SurfaceView.class;
                f.rect = c.getDeclaredField("mScreenRect");
                f.left = c.getDeclaredField("mWindowSpaceLeft");
                f.top = c.getDeclaredField("mWindowSpaceTop");
                f.apply = c.getDeclaredMethod("setParentSpaceRectangle", android.graphics.Rect.class, long.class);
                f.rect.setAccessible(true);
                f.left.setAccessible(true);
                f.top.setAccessible(true);
                f.apply.setAccessible(true);
            } catch (Exception e) {
                android.util.Log.w("ArelInsets", "api26 fix not installed: " + e);
                return;
            }
            // Add the listener after SurfaceView.onAttachedToWindow, so that it runs after updateSurface.
            sv.addOnAttachStateChangeListener(f);
            if (sv.isAttachedToWindow()) f.onViewAttachedToWindow(sv);
        }

        @Override
        public void onViewAttachedToWindow(View v) {
            v.getViewTreeObserver().removeOnPreDrawListener(this);
            v.getViewTreeObserver().addOnPreDrawListener(this);
        }

        @Override
        public void onViewDetachedFromWindow(View v) { v.getViewTreeObserver().removeOnPreDrawListener(this); }

        @Override
        public boolean onPreDraw() {
            if (!dead) {
                try { check(); } catch (Exception e) {
                    dead = true;
                    android.util.Log.w("ArelInsets", "api26 fix stopped: " + e);
                }
            }
            return true;
        }

        private void check() throws Exception {
            android.graphics.Rect r = (android.graphics.Rect) rect.get(sv);
            if (r == null || r.isEmpty()) return;
            int l = left.getInt(sv), t = top.getInt(sv);
            int rr = l + sv.getWidth(), b = t + sv.getHeight();
            if (r.left != l || r.top != t || (r.right == rr && r.bottom == b)) return;
            r.set(l, t, rr, b);
            apply.invoke(sv, r, -1L);
            android.util.Log.i("ArelInsets", "api26 surface rect fixed " + l + " " + t + " " + rr + " " + b);
        }
    }

    /** Hides the system bars. install() calls it, and again at each window focus gain. */
    public static void hideBars(Activity a) {
        Window w = a.getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else if (Build.VERSION.SDK_INT >= 19) {
            w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /** The insets left, top, right, bottom that the game must not cover. Hidden bars give 0. */
    static int[] safe(WindowInsets in) {
        if (Build.VERSION.SDK_INT >= 30) {
            android.graphics.Insets i = in.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            return new int[] { i.left, i.top, i.right, i.bottom };
        }
        int l = in.getSystemWindowInsetLeft(), t = in.getSystemWindowInsetTop();
        int r = in.getSystemWindowInsetRight(), b = in.getSystemWindowInsetBottom();
        if (Build.VERSION.SDK_INT >= 28 && in.getDisplayCutout() != null) {
            android.view.DisplayCutout d = in.getDisplayCutout();
            l = Math.max(l, d.getSafeInsetLeft()); t = Math.max(t, d.getSafeInsetTop());
            r = Math.max(r, d.getSafeInsetRight()); b = Math.max(b, d.getSafeInsetBottom());
        }
        return new int[] { l, t, r, b };
    }

    static void fit(View root, int pw, int ph, int[] s) {
        int sw = pw - s[0] - s[2], sh = ph - s[1] - s[3];
        if (sw <= 0 || sh <= 0) return;
        int w = sw, h = sw * 2 / 3;
        if (h > sh) { h = sh; w = sh * 3 / 2; }
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h, Gravity.LEFT | Gravity.TOP);
        lp.leftMargin = s[0] + (sw - w) / 2;
        lp.topMargin = s[1] + (sh - h) / 2;
        ViewGroup.LayoutParams old = root.getLayoutParams();
        if (old instanceof FrameLayout.LayoutParams) {
            FrameLayout.LayoutParams o = (FrameLayout.LayoutParams) old;
            if (o.width == w && o.height == h && o.leftMargin == lp.leftMargin && o.topMargin == lp.topMargin
                && o.gravity == lp.gravity) return;
        }
        root.setLayoutParams(lp);
        // Gates and macros read this line (emu scripts/play): the game box in window pixels, and the safe insets.
        android.util.Log.i("ArelInsets", "box " + lp.leftMargin + " " + lp.topMargin + " " + w + " " + h
            + " window " + pw + " " + ph + " safe " + s[0] + " " + s[1] + " " + s[2] + " " + s[3]);
    }
}
