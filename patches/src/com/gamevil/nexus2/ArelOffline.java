package com.gamevil.nexus2;

import android.widget.Toast;

/** Shown when a real-money purchase is tapped. Reports nothing to native code, so nothing can be credited. */
public final class ArelOffline implements Runnable {
    private final boolean korean;

    public ArelOffline(boolean korean) {
        this.korean = korean;
    }

    @Override
    public void run() {
        String text = korean
            ? "오프라인 버전에서는 구매할 수 없습니다."
            : "Purchases are unavailable in this offline version.";
        Toast.makeText(NexusGLActivity.myActivity, text, Toast.LENGTH_LONG).show();
    }
}
