package com.kamisquadx.megamanxrecomp;

import android.content.pm.ActivityInfo;

import org.libsdl.app.SDLActivity;

/** The game: SDL3's activity running libmain.so (the native host). */
public final class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" }; /* SDL3 is linked into libmain.so */
    }

    /* SDL picks an orientation from the window shape; a resizable window lets
     * it fall back to portrait. The game is landscape-only (either way up). */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }
}
