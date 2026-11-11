package io.github.cailet0422.acwwkor;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** The first screen: once the app has a copy of the player's ROM (files/game.nds) it goes
 *  straight to the game; until then it asks for one with the system's file picker and copies it
 *  in. The game checks the copy (port/src/rom_prep.c) and removes it if it is not the Korean
 *  release, so the next start asks again. Nothing from the ROM ever leaves the app's files. */
public class LaunchActivity extends Activity {
    private static final int PICK_ROM = 1;

    private TextView status;
    private Button pick;

    private File rom() {
        return new File(getFilesDir(), "game.nds");
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (rom().isFile()) {
            startGame();
            return;
        }
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(pad, pad, pad, pad);
        column.setGravity(Gravity.CENTER_HORIZONTAL);

        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(24);
        column.addView(title);

        TextView body = new TextView(this);
        body.setText(R.string.rom_explain);
        body.setTextSize(16);
        body.setPadding(0, pad / 2, 0, pad);
        column.addView(body);

        pick = new Button(this);
        pick.setText(R.string.rom_pick);
        pick.setOnClickListener(v -> pickRom());
        column.addView(pick, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        status = new TextView(this);
        status.setPadding(0, pad / 2, 0, 0);
        column.addView(status);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(column);
        setContentView(scroll);
    }

    private void pickRom() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM || result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri uri = data.getData();
        pick.setEnabled(false);
        status.setText(R.string.rom_copying);
        new Thread(() -> {
            final boolean ok = copy(uri);
            runOnUiThread(() -> {
                if (ok) {
                    startGame();
                } else {
                    pick.setEnabled(true);
                    status.setText(R.string.rom_copy_failed);
                }
            });
        }).start();
    }

    /** The picked file to files/game.nds, through a temporary name so a half copy is never
     *  taken for a ROM. */
    private boolean copy(Uri uri) {
        File part = new File(getFilesDir(), "game.nds.part");
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(part)) {
            if (in == null) {
                return false;
            }
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) {
                out.write(buf, 0, n);
            }
        } catch (IOException | SecurityException e) {
            part.delete();
            return false;
        }
        return part.renameTo(rom());
    }

    private void startGame() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }
}
