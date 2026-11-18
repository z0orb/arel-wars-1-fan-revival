package com.gamevil.nexus2;

import android.app.Activity;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;

/**
 * The offline purchase message of v1 patch 0006-offline (ArelOffline), for target 35 (emu PLAN.md DES-11).
 * From target 30, the system draws a text Toast itself. From Android 12 it shows the app icon and at most
 * two lines, so the v1 text "Purchases are unavailable in this offline version." shows cut off.
 * From API 30 this class shows the same text, for the same time (Toast.LENGTH_LONG, 3.5 s), in a view on
 * top of the game box, at the place of the v1 Toast. Below API 30 it shows the v1 Toast.
 * The view takes no touch. The game below it gets each touch, as with a Toast.
 */
public final class ArelNotice {
    static final long LONG_MS = 3500;
    private static View shown;

    private ArelNotice() {}

    public static void show(final Activity a, final CharSequence text) {
        if (android.os.Looper.myLooper() != android.os.Looper.getMainLooper()) {
            a.runOnUiThread(new Runnable() {
                @Override
                public void run() { show(a, text); }
            });
            return;
        }
        if (Build.VERSION.SDK_INT < 30) {
            Toast.makeText(a, text, Toast.LENGTH_LONG).show();
            return;
        }
        View root = a.findViewById(ArelInsets.ROOT_ID);
        if (root == null || !(root.getParent() instanceof FrameLayout) || root.getHeight() <= 0) {
            Toast.makeText(a, text, Toast.LENGTH_LONG).show();
            return;
        }
        final FrameLayout parent = (FrameLayout) root.getParent();
        hide(parent);
        int w = root.getWidth(), h = root.getHeight();
        // Sizes in parts of the game box, so the message has the same place and size on each screen.
        // On the owner's phone (box 1620 x 1080) they give the size of the v1 Toast.
        TextView tv = new TextView(a);
        tv.setText(text);
        tv.setTextColor(0xff1f1f1f);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_PX, h * 0.042f);
        tv.setTypeface(android.graphics.Typeface.MONOSPACE);
        tv.setGravity(Gravity.CENTER);
        tv.setMaxWidth((int) (w * 0.40f));
        int px = (int) (h * 0.05f), py = (int) (h * 0.035f);
        tv.setPadding(px, py, px, py);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xfff7f7f7);
        bg.setCornerRadius(h * 0.03f);
        tv.setBackground(bg);
        FrameLayout.LayoutParams tl = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.CENTER_HORIZONTAL | Gravity.BOTTOM);
        tl.bottomMargin = (int) (h * 0.175f);
        FrameLayout holder = new FrameLayout(a);
        holder.addView(tv, tl);
        // The holder covers the game box exactly.
        FrameLayout.LayoutParams hl = new FrameLayout.LayoutParams(w, h, Gravity.LEFT | Gravity.TOP);
        hl.leftMargin = root.getLeft();
        hl.topMargin = root.getTop();
        parent.addView(holder, hl);
        shown = holder;
        final View mine = holder;
        parent.postDelayed(new Runnable() {
            @Override
            public void run() { if (shown == mine) hide(parent); }
        }, LONG_MS);
    }

    static void hide(ViewGroup parent) {
        if (shown != null) {
            parent.removeView(shown);
            shown = null;
        }
    }
}
