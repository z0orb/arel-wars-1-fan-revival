package com.gamevil.nexus2;

import android.app.Activity;
import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.webkit.WebView;
import android.widget.FrameLayout;

/**
 * E6: the v1 layouts hold a WebView (the terms page in DRMLicensing, the help page in the game activity).
 * With no WebView provider (an image with no WebView, or WebView disabled or in an update), the inflater
 * throws, and the activity does not start. This factory makes the WebView with the same constructor as the
 * inflater. Only if that throws, it puts an empty FrameLayout with the same attributes in its place.
 * The smali patch 0003-webview.patch calls loadUrl here, so the shell code skips a view that is not a WebView.
 * emu/docs/DIVERGENCES.md row 12.
 */
public final class ArelWeb implements LayoutInflater.Factory2 {
    private static final String TAG = "ArelWeb";

    /** Call before setContentView. */
    public static void install(Activity a) {
        try {
            LayoutInflater li = a.getLayoutInflater();
            if (li.getFactory() == null) li.setFactory2(new ArelWeb());
        } catch (RuntimeException e) {
            Log.w(TAG, "factory not set: " + e);
        }
    }

    /** The shell calls this in place of WebView.loadUrl. */
    public static void loadUrl(View v, String url) {
        if (v instanceof WebView) ((WebView) v).loadUrl(url);
    }

    @Override
    public View onCreateView(View parent, String name, Context context, AttributeSet attrs) {
        // The layouts use WebView and its subclass com.gamevil.nexus2.ui.UIWebView. Other views: the inflater.
        String cn = "WebView".equals(name) ? "android.webkit.WebView" : name;
        if (cn.indexOf('.') < 0) return null;
        Class<?> c;
        try {
            c = Class.forName(cn, false, context.getClassLoader());
        } catch (ClassNotFoundException | LinkageError e) {
            return null;
        }
        if (!WebView.class.isAssignableFrom(c)) return null;
        try {
            return (View) c.getConstructor(Context.class, AttributeSet.class).newInstance(context, attrs);
        } catch (java.lang.reflect.InvocationTargetException e) {
            Log.w(TAG, "no WebView (" + cn + "), an empty view in its place: " + e.getCause());
            return new FrameLayout(context, attrs);
        } catch (ReflectiveOperationException e) {
            return null;
        }
    }

    @Override
    public View onCreateView(String name, Context context, AttributeSet attrs) {
        return onCreateView(null, name, context, attrs);
    }
}
