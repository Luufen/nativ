/* The first screen of a game exported by vitarecomp export-apk (pipeline/apk.py): it puts the game's files (app0) into
 * the app's data folder (Android/data/<package>/files/fs/app0), then starts the game (NativActivity).
 * assets/nativ/data.txt says how they come:
 *   mode inside   the APK carries them (assets/nativ-data/app0.zip): unpacked on the first start, with a progress bar
 *   mode apart    a data file the player copied to the device beside the APK (<name>-data.zip): picked in Android's
 *                 file picker -- or opened with this app from a file manager -- and unpacked from there
 *   id <hex>      which files: a data file names its id in its first entry (nativ-data.txt); another game's is refused.
 *                 fs/app0.id records the id unpacked: a new APK with other files unpacks again
 * Files pushed by `vitarecomp export-apk --install` (adb) come with their app0.id: nothing to unpack. */
package org.projectnativ.app;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.StatFs;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class SetupActivity extends Activity {
    private static final String TAG = "Nativ";
    private static final int PICK = 1;
    private final HashMap<String, String> info = new HashMap<>();
    private TextView text;
    private ProgressBar bar;
    private Button pick;
    private File fs;
    private volatile boolean busy;

    private static String read(InputStream in) throws Exception {
        StringBuilder s = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8))) {
            for (String l; (l = r.readLine()) != null; ) s.append(l).append('\n');
        }
        return s.toString();
    }

    private static void parse(String s, HashMap<String, String> into) {
        for (String l : s.split("\n")) { int i = l.indexOf(' '); if (i > 0) into.put(l.substring(0, i), l.substring(i + 1).trim()); }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File ext = getExternalFilesDir(null);
        fs = new File(ext != null ? ext : getFilesDir(), "fs");
        try { parse(read(getAssets().open("nativ/data.txt")), info); } catch (Exception e) { Log.e(TAG, "no data.txt", e); }
        if (installed()) { play(); return; }
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL); box.setGravity(Gravity.CENTER); box.setPadding(96, 64, 96, 64);
        box.setBackgroundColor(Color.rgb(18, 20, 26));
        text = new TextView(this); text.setTextColor(Color.rgb(230, 232, 238)); text.setTextSize(20); text.setGravity(Gravity.CENTER);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); bar.setMax(1000);
        pick = new Button(this); pick.setText("Choose the game's data file");
        pick.setOnClickListener(v -> choose());
        box.addView(text); box.addView(bar); box.addView(pick);
        setContentView(box);
        String title = getApplicationInfo().loadLabel(getPackageManager()).toString();
        if ("inside".equals(info.get("mode"))) {
            pick.setVisibility(android.view.View.GONE);
            text.setText(title + "\n\nPreparing the game's files (only this once)...");
            unpack(null, parseLong(info.get("bytes")));
        } else {
            bar.setVisibility(android.view.View.GONE);
            text.setText(title + "\n\nThe game's files are not here yet. Copy " + info.get("file")
                    + " (made with this APK) to this device, then choose it here -- or open it with this app from your file manager.");
            Uri u = getIntent() != null && Intent.ACTION_VIEW.equals(getIntent().getAction()) ? getIntent().getData() : null;
            if (u != null) fromUri(u);
        }
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        if (i != null && Intent.ACTION_VIEW.equals(i.getAction()) && i.getData() != null && !busy) fromUri(i.getData());
    }

    private static long parseLong(String s) { try { return Long.parseLong(s); } catch (Exception e) { return 0; } }

    private boolean installed() {
        try {
            File id = new File(fs, "app0.id");
            return id.isFile() && read(new FileInputStream(id)).trim().equals(info.get("id")) && new File(fs, "app0").isDirectory();
        } catch (Exception e) { return false; }
    }

    private void play() {
        startActivity(new Intent(this, NativActivity.class));
        finish();
    }

    private void choose() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        startActivityForResult(i, PICK);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == PICK && res == RESULT_OK && data != null && data.getData() != null) fromUri(data.getData());
    }

    private void fromUri(Uri u) {
        long size = 0;
        try (Cursor c = getContentResolver().query(u, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) size = c.getLong(0);
        } catch (Exception ignored) { }
        pick.setEnabled(false); bar.setVisibility(android.view.View.VISIBLE);
        text.setText("Importing the game's files...");
        unpack(u, size > 0 ? size : parseLong(info.get("bytes")));
    }

    private void fail(String why) {
        runOnUiThread(() -> {
            busy = false;
            text.setText(why); bar.setVisibility(android.view.View.GONE);
            if (!"inside".equals(info.get("mode"))) { pick.setEnabled(true); pick.setVisibility(android.view.View.VISIBLE); }
        });
    }

    /* the zip (the APK's own when u is null) into fs/: nativ-data.txt first (its id must be this APK's), then app0/... */
    private void unpack(Uri u, long total) {
        busy = true;
        new Thread(() -> {
            try {
                long need = parseLong(info.get("bytes"));
                if (new StatFs(fs.getParentFile().getPath()).getAvailableBytes() < need + (64L << 20)) {
                    fail("Not enough free space: the game's files need " + (need >> 20) + " MB."); return;
                }
                fs.mkdirs();
                File app0 = new File(fs, "app0"), id = new File(fs, "app0.id");
                id.delete(); delete(app0);
                String base = fs.getCanonicalPath() + File.separator;
                long done = 0; byte[] buf = new byte[1 << 20]; boolean checked = false;
                try (InputStream raw = u == null ? getAssets().open("nativ-data/app0.zip") : getContentResolver().openInputStream(u);
                     ZipInputStream z = new ZipInputStream(raw)) {
                    for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                        String n = e.getName();
                        if (!checked) {
                            if (!n.equals("nativ-data.txt")) { fail("That isn't a data file of a game exported by Project Nativ."); return; }
                            HashMap<String, String> d = new HashMap<>(); parse(read(new NonClosing(z)), d);
                            if (!String.valueOf(info.get("id")).equals(d.get("id"))) {
                                fail("That data file belongs to " + d.get("title") + " or another export of it, not to this APK. Choose " + info.get("file") + "."); return;
                            }
                            checked = true; continue;
                        }
                        File f = new File(fs, n);
                        if (!f.getCanonicalPath().startsWith(base) || !n.startsWith("app0/")) { fail("The data file holds an unexpected entry: " + n); return; }
                        if (e.isDirectory()) { f.mkdirs(); continue; }
                        f.getParentFile().mkdirs();
                        try (OutputStream out = new FileOutputStream(f)) {
                            for (int k; (k = z.read(buf)) > 0; ) {
                                out.write(buf, 0, k); done += k;
                                final int p = total > 0 ? (int) Math.min(1000, done * 1000 / total) : 0;
                                runOnUiThread(() -> bar.setProgress(p));
                            }
                        }
                    }
                }
                if (!checked) { fail("The data file is empty."); return; }
                try (OutputStream out = new FileOutputStream(id)) { out.write((info.get("id") + "\n").getBytes(StandardCharsets.UTF_8)); }
                Log.i(TAG, "the game's files unpacked: " + (done >> 20) + " MB into " + fs);
                runOnUiThread(this::play);
            } catch (Exception e) {
                Log.e(TAG, "unpacking the game's files", e);
                fail("The game's files could not be unpacked: " + e.getMessage());
            }
        }).start();
    }

    private static void delete(File f) {
        File[] l = f.listFiles();
        if (l != null) for (File c : l) delete(c);
        f.delete();
    }

    /* the zip's current entry as a stream whose close() leaves the zip open */
    private static final class NonClosing extends java.io.FilterInputStream {
        NonClosing(InputStream in) { super(in); }
        @Override public void close() { }
    }
}
