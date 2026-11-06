package com.kamisquadx.megamanxrecomp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/** Runs in its own process (see the manifest): ends the game's process, then
 *  starts the game again in a fresh one. Used when the game relaunches itself
 *  (a new ROM, or a setting that needs a restart). */
public final class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if (pid > 0) Process.killProcess(pid);
        Intent game = new Intent(this, MainActivity.class);
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(game);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
