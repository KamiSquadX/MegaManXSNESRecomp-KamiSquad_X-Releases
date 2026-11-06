package com.kamisquadx.megamanxrecomp;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Process;
import android.util.Log;

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
