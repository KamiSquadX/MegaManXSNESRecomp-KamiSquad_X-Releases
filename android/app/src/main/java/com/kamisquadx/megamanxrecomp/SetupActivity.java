package com.kamisquadx.megamanxrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;

/**
 * First-launch setup. No ROM is bundled with the APK.
 *
 * The native host makes getExternalFilesDir(null) its working directory and
 * its "executable folder", so config.ini, rom.cfg, mods/ and saves/ all live
 * there (and can be reached over USB / adb). This screen copies the runtime
 * files from the APK into it, imports the player's own Mega Man X (USA) ROM
 * as mmx.sfc (named in rom.cfg) and, optionally, the Mega Man X2 (USA) ROM
 * (X2 weapons, armor and bosses) and the Mega Man X3 (USA) ROM that Zero,
 * co-op and the X3 weapons are built from.
 */
public final class SetupActivity extends Activity {
    private static final int PICK_X = 1001, PICK_X3 = 1002, PICK_X2 = 1003;
    private static final String X_ROM = "mmx.sfc", X2_ROM = "mmx2.sfc", X3_ROM = "mmx3.sfc";
    /* Normalized (copier header removed) SHA-256 of each supported dump. */
    private static final String X_SHA256 =
            "b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32";
    private static final String X2_SHA256 =
            "f3246755f608a1e1dc9c848b61da3b824c7853b29b3be40df6fc7f2793a887ed";
    private static final String X3_SHA256 =
            "65b03268afac296330e8ff8d60dd0825879e13ed658b37713c034a3bd074f1d7";

    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            installRuntimeFiles();
        } catch (Exception e) {
            show("Couldn't install the game's files: " + e.getMessage());
            return;
        }
        if (!getIntent().getBooleanExtra("setup", false) && romOk(X_ROM, X_SHA256)) {
            launchGame();
            return;
        }
        show(null);
    }

    private void show(String message) {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setTextSize(16);
        status.setText(message != null ? message : summary());
        root.addView(status);

        Button pickX = new Button(this);
        pickX.setText("Select Mega Man X (USA) ROM");
        pickX.setOnClickListener(v -> pick(PICK_X));
        root.addView(pickX);

        Button pickX2 = new Button(this);
        pickX2.setText("Select Mega Man X2 (USA) ROM (optional: X2 weapons, armor and bosses)");
        pickX2.setOnClickListener(v -> pick(PICK_X2));
        root.addView(pickX2);

        Button pickX3 = new Button(this);
        pickX3.setText("Select Mega Man X3 (USA) ROM (optional: Zero, co-op, X3 weapons)");
        pickX3.setOnClickListener(v -> pick(PICK_X3));
        root.addView(pickX3);

        Button play = new Button(this);
        play.setText("Play");
        play.setEnabled(romOk(X_ROM, X_SHA256));
        play.setOnClickListener(v -> launchGame());
        root.addView(play);

        setContentView(root);
        goFullscreen();
    }

    private String summary() {
        StringBuilder s = new StringBuilder("Mega Man X Recompiled ");
        s.append(BuildConfig.VERSION_NAME).append("\n\n");
        s.append(romOk(X_ROM, X_SHA256) ? "Mega Man X ROM: ready\n"
                : "Select your own Mega Man X (USA) ROM (.sfc or .smc) to play.\n");
        s.append(romOk(X2_ROM, X2_SHA256) ? "Mega Man X2 ROM: ready\n"
                : "Mega Man X2 (USA) ROM: not selected (needed for X2 weapons, armor and bosses).\n");
        s.append(romOk(X3_ROM, X3_SHA256) ? "Mega Man X3 ROM: ready\n"
                : "Mega Man X3 (USA) ROM: not selected (needed for Zero, co-op and X3 weapons).\n");
        s.append("\nFiles: ").append(filesDir().getAbsolutePath());
        return s.toString();
    }

    private void pick(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, request);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final boolean x = requestCode == PICK_X, x2 = requestCode == PICK_X2;
        if (!x && !x2 && requestCode != PICK_X3) return;
        final String rom = x ? X_ROM : x2 ? X2_ROM : X3_ROM;
        try {
            importRom(data.getData(), rom, x ? X_SHA256 : x2 ? X2_SHA256 : X3_SHA256);
            if (x) writeText(new File(filesDir(), "rom.cfg"), new File(filesDir(), X_ROM).getAbsolutePath() + "\n");
            else linkSource(x2 ? "x2" : "x3", rom);
            show(null);
        } catch (Exception e) {
            show((x ? "That file is not Mega Man X (USA) (Rev 1). "
                    : x2 ? "That file is not Mega Man X2 (USA). " : "That file is not Mega Man X3 (USA). ")
                    + e.getMessage() + "\n\n" + summary());
        }
    }

    /* The mods read the X2 and X3 ROMs through the shared resources
     * megaman-x.source.x2 (X2 weapons, armor, bosses) and megaman-x.source.x3
     * (co-op, Zero, X3 weapons) in mods/preloaded/state.toml. Point the one
     * for `game` at the imported copy, keeping every other line the launcher
     * wrote. */
    private void linkSource(String game, String rom) throws Exception {
        File state = new File(filesDir(), "mods/preloaded/state.toml");
        String path = new File(filesDir(), rom).getAbsolutePath();
        String entry = "[[shared_resource]]\nid = \"megaman-x.source." + game + "\"\npath = \"" + path + "\"\n";
        String text = state.isFile() ? readText(state) : "format_version = 1\n";
        text = text.replaceAll("(?s)\\[\\[shared_resource\\]\\]\\s*\\nid = \"megaman-x\\.source\\." + game + "\"\\s*\\npath = \"[^\"]*\"\\s*\\n?", "");
        if (!text.endsWith("\n")) text += "\n";
        writeText(state, text + "\n" + entry);
    }

    private File filesDir() {
        File dir = getExternalFilesDir(null);
        if (dir == null) throw new IllegalStateException("Android's app files folder is unavailable");
        return dir;
    }

    private boolean romOk(String name, String sha) {
        File f = new File(filesDir(), name);
        try {
            return f.isFile() && sha.equalsIgnoreCase(normalizedSha256(f));
        } catch (Exception e) {
            return false;
        }
    }

    private void importRom(Uri uri, String name, String sha) throws Exception {
        File dest = new File(filesDir(), name);
        File tmp = new File(filesDir(), name + ".tmp");
        tmp.delete();
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new IllegalStateException("could not open the file");
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
        if (!sha.equalsIgnoreCase(normalizedSha256(tmp))) {
            tmp.delete();
            throw new IllegalStateException("(SHA-256 mismatch)");
        }
        dest.delete();
        if (!tmp.renameTo(dest)) {
            tmp.delete();
            throw new IllegalStateException("could not save the copy");
        }
    }

    /* Raw .sfc dumps and .smc files with a 512-byte copier header hash alike. */
    private static String normalizedSha256(File file) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (FileInputStream in = new FileInputStream(file)) {
            if ((file.length() & 0x7fffL) == 512L) {
                long remaining = 512;
                while (remaining > 0) {
                    long skipped = in.skip(remaining);
                    if (skipped <= 0) throw new IllegalStateException("could not skip the SMC header");
                    remaining -= skipped;
                }
            }
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
        }
        StringBuilder hex = new StringBuilder(64);
        for (byte b : md.digest()) hex.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return hex.toString();
    }

    /* The APK's payload/ tree (launcher art, fonts, mod catalog, logging.ini),
     * refreshed whenever the app version changes. Player files (config.ini,
     * state.toml, saves) are never in it, so they are never overwritten. */
    private void installRuntimeFiles() throws Exception {
        SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
        String installed = prefs.getString("payload", "");
        if (installed.equals(BuildConfig.VERSION_NAME) && new File(filesDir(), "assets").isDirectory()) return;
        copyAssetTree(getAssets(), "payload", filesDir());
        prefs.edit().putString("payload", BuildConfig.VERSION_NAME).apply();
    }

    private static void copyAssetTree(AssetManager am, String path, File dest) throws Exception {
        String[] children = am.list(path);
        if (children == null || children.length == 0) {
            File parent = dest.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                throw new IllegalStateException("could not create " + parent);
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[32 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
            return;
        }
        if (!dest.isDirectory() && !dest.mkdirs()) throw new IllegalStateException("could not create " + dest);
        for (String child : children) copyAssetTree(am, path + "/" + child, new File(dest, child));
    }

    private static String readText(File f) throws Exception {
        try (InputStream in = new FileInputStream(f)) {
            byte[] data = new byte[(int) f.length()];
            int off = 0, n;
            while (off < data.length && (n = in.read(data, off, data.length - off)) > 0) off += n;
            return new String(data, 0, off, StandardCharsets.UTF_8);
        }
    }

    private static void writeText(File f, String text) throws Exception {
        File parent = f.getParentFile();
        if (parent != null && !parent.isDirectory()) parent.mkdirs();
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private void launchGame() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goFullscreen();
    }

    private void goFullscreen() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
