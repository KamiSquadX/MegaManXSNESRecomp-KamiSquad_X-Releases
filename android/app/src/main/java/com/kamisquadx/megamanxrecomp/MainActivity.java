package com.kamisquadx.megamanxrecomp;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Handler;
import android.os.Process;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** The game: SDL3's activity running libmain.so (the native host). */
public final class MainActivity extends SDLActivity {
    /** Left by the native host (host_relaunch.c) when it wants to start over
     *  with new arguments: one per line, in the working directory. */
    private static final String RELAUNCH_ARGS = "relaunch.args";

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

    /* The navigation and status bars stay hidden for the whole session, the
     * launcher included. SDL hides them only while its window is fullscreen
     * and shows them again whenever it is told otherwise (its activity's
     * start, the launcher's window coming and going), so they are hidden
     * again here whenever they come back. A swipe still shows them for a
     * moment (immersive sticky).
     *
     * A windowed SDL window also sets FLAG_FORCE_NOT_FULLSCREEN, which keeps
     * the bars on screen whatever the flags below say. A fresh install starts
     * windowed (Fullscreen = 0 until the settings are saved), so that flag is
     * cleared here too, and Android 11+ is told through its insets
     * controller as well. */
    private static final int IMMERSIVE_FLAGS = View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;

    private final Runnable hideSystemBars = new Runnable() {
        @Override
        public void run() {
            Window window = getWindow();
            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.getDecorView().setSystemUiVisibility(IMMERSIVE_FLAGS);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    controller.hide(WindowInsets.Type.systemBars());
                }
            }
        }
    };

    /* SDL changes the window style (and its bars) from its own handler after
     * the activity starts; hide them again shortly after each resume too. */
    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBarsSoon();
        Handler handler = getWindow().getDecorView().getHandler();
        if (handler != null) handler.postDelayed(hideSystemBars, 500);
    }

    private void hideSystemBarsSoon() {
        Handler handler = getWindow().getDecorView().getHandler();
        if (handler == null) {
            hideSystemBars.run();
            return;
        }
        handler.removeCallbacks(hideSystemBars);
        handler.post(hideSystemBars);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBarsSoon();
    }

    @Override
    public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        if ((visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0
                || (visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0)
            hideSystemBarsSoon();
    }

    private File relaunchFile() {
        File dir = getExternalFilesDir(null);
        return dir == null ? null : new File(dir, RELAUNCH_ARGS);
    }

    /** The arguments a relaunch asked for (read once, then deleted). */
    @Override
    protected String[] getArguments() {
        File f = relaunchFile();
        if (f == null || !f.isFile()) return new String[0];
        List<String> args = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(
                new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            for (String line; (line = r.readLine()) != null; )
                if (!line.isEmpty()) args.add(line);
        } catch (Exception e) {
            Log.w("MMX", "cannot read " + f + ": " + e);
        }
        if (!f.delete()) Log.w("MMX", "cannot delete " + f);
        return args.toArray(new String[0]);
    }

    /* The native host keeps process-wide state, so a relaunch needs a fresh
     * process: RestartActivity (its own process) ends this one and starts the
     * game again, which then reads relaunch.args. */
    @Override
    protected void main() {
        super.main();
        File f = relaunchFile();
        if (f != null && f.isFile()) {
            Log.i("MMX", "relaunching with " + f);
            Intent i = new Intent(this, RestartActivity.class);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            i.putExtra(RestartActivity.EXTRA_PID, Process.myPid());
            startActivity(i);
        }
    }
}
