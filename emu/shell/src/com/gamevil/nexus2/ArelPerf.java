package com.gamevil.nexus2;

import android.os.SystemClock;
import android.util.Log;

import java.util.Arrays;
import java.util.Locale;

/**
 * The frame interval log of emu PLAN.md DES-16 (Phase E7). The smali patch 0004-perf calls begin() just
 * before and end() just after Natives.NativeRender() in NexusGLRenderer.drawFrame (the GL thread).
 * The game does one logic step for each NativeRender call, so the frame rate is also the logic rate.
 * Every 2 s it writes two lines with the tag ArelEmuPerf:
 *   perf total T frames N sec S fps F p50 A p95 B max C render R
 *   hist K:C K:C ...
 * T: all frames since the start of the process. N: frame intervals in this window. S: window length in
 * seconds (first begin to last begin). F: N / S. A, B, C: frame intervals (begin to begin) in ms.
 * R: mean time in NativeRender in ms. hist: the intervals of the window in 0.5 ms buckets
 * (K = round(ms * 2), the last bucket holds all longer intervals). The bench adds the buckets of
 * all windows of a battle, to get the p95 of the whole battle.
 * Debug and release builds. Cost: two clock reads and one array store for each frame.
 */
public final class ArelPerf {
    static final String TAG = "ArelEmuPerf";
    static final long WINDOW_NS = 2000000000L;
    private static final int CAP = 4096;
    private static final int BUCKETS = 401;
    private static final long[] iv = new long[CAP];
    private static final int[] hist = new int[BUCKETS];
    private static int n;
    private static long total;
    private static long last;
    private static long winStart;
    private static long start;
    private static long renderSum;

    private ArelPerf() {}

    public static void begin() {
        long t = SystemClock.elapsedRealtimeNanos();
        if (last == 0) winStart = t;
        else if (n < CAP) iv[n++] = t - last;
        last = t;
        start = t;
    }

    public static void end() {
        long t = SystemClock.elapsedRealtimeNanos();
        renderSum += t - start;
        total++;
        if (t - winStart < WINDOW_NS || n == 0) return;
        long[] s = Arrays.copyOf(iv, n);
        Arrays.sort(s);
        double sec = (last - winStart) / 1e9;
        Log.i(TAG, String.format(Locale.ROOT,
                "perf total %d frames %d sec %.3f fps %.2f p50 %.2f p95 %.2f max %.2f render %.2f",
                total, n, sec, n / sec, ms(s[(n - 1) / 2]), ms(s[(int) Math.ceil(n * 0.95) - 1]), ms(s[n - 1]),
                renderSum / 1e6 / n));
        Arrays.fill(hist, 0);
        for (int i = 0; i < n; i++) hist[(int) Math.min(BUCKETS - 1, Math.round(s[i] / 5e5))]++;
        StringBuilder b = new StringBuilder("hist");
        for (int k = 0; k < BUCKETS; k++) if (hist[k] != 0) b.append(' ').append(k).append(':').append(hist[k]);
        Log.i(TAG, b.toString());
        n = 0;
        renderSum = 0;
        winStart = last;
    }

    private static double ms(long ns) { return ns / 1e6; }
}
