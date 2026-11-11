package io.github.cailet0422.acwwkor;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** The settings, opened from the game (the mark with three dots).
 *
 *  The choices are lines of files/settings.txt, which the game reads when it starts and again
 *  when it comes back to the front (port/src/front_sdl.c): only the lines this screen knows
 *  are rewritten, the rest (the buttons' arrangement) are kept as they are. The save can be
 *  copied out to a file and replaced by one, and another ROM chosen; after either of the last
 *  two the app closes, since the running game holds the old ones. */
public class SettingsActivity extends Activity {
    private static final int EXPORT_SAVE = 1;
    private static final int IMPORT_SAVE = 2;
    private static final int EXPORT_LOG = 3;

    private File settingsFile() {
        return new File(getFilesDir(), "settings.txt");
    }

    private File saveFile() {
        return new File(getFilesDir(), "acww.sav");
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.settings_title);
        title.setTextSize(22);
        title.setPadding(0, 0, 0, pad / 2);
        column.addView(title);

        column.addView(button(R.string.settings_back, this::finish));
        column.addView(choice("sound", true, R.string.settings_sound));
        column.addView(choice("resetti", false, R.string.settings_resetti));
        column.addView(choice("full_touch", false, R.string.settings_full_touch));
        column.addView(choice("tall", true, R.string.settings_tall));
        column.addView(resolution());
        /* a request the game takes when it is back in front: the arranging opens */
        column.addView(button(R.string.settings_arrange, () -> {
            write("full_touch", false);
            write("arrange", true);
            finish();
        }));

        TextView saveTitle = new TextView(this);
        saveTitle.setText(R.string.settings_save_title);
        saveTitle.setTextSize(18);
        saveTitle.setPadding(0, pad, 0, pad / 4);
        column.addView(saveTitle);
        column.addView(button(R.string.settings_save_export, () -> {
            if (!saveFile().isFile()) {
                toast(R.string.settings_save_none);
                return;
            }
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_TITLE, "acww.sav");
            startActivityForResult(intent, EXPORT_SAVE);
        }));
        column.addView(button(R.string.settings_save_import, () -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, IMPORT_SAVE);
        }));
        column.addView(button(R.string.settings_rom_change, () -> new AlertDialog.Builder(this)
                .setMessage(R.string.settings_rom_change_ask)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    new File(getFilesDir(), "game.nds").delete();
                    closeApp();
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show()));

        column.addView(button(R.string.settings_log_export, () -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TITLE, "acww-kor-port-log.txt");
            startActivityForResult(intent, EXPORT_LOG);
        }));

        TextView note = new TextView(this);
        note.setText(R.string.settings_note);
        note.setPadding(0, pad, 0, 0);
        column.addView(note);

        /* how the run is going (front_sdl.c, status_write): something to read out in a report */
        String status = status();
        String crash = crashLines();
        if (!crash.isEmpty()) {
            status = status + "\n\n" + getString(R.string.settings_crashed) + "\n" + crash;
        }
        if (!status.isEmpty()) {
            TextView state = new TextView(this);
            state.setText(getString(R.string.settings_status) + "\n" + status);
            state.setTextIsSelectable(true);
            state.setPadding(0, pad, 0, 0);
            column.addView(state);
        }

        ScrollView scroll = new ScrollView(this);
        scroll.addView(column);
        setContentView(scroll);
    }

    private CheckBox choice(String key, boolean byDefault, int label) {
        CheckBox box = new CheckBox(this);
        box.setText(label);
        box.setTextSize(16);
        box.setChecked(read(key, byDefault));
        box.setOnCheckedChangeListener((b, on) -> write(key, on));
        return box;
    }

    /** The 3D scene's size: 1, 2 or 3 times the DS's, taking effect at the next start. */
    private LinearLayout resolution() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView label = new TextView(this);
        label.setText(R.string.settings_resolution);
        label.setTextSize(16);
        box.addView(label);
        RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.HORIZONTAL);
        int now = readNumber("resolution", 1);
        for (int n = 1; n <= 3; n++) {
            RadioButton b = new RadioButton(this);
            b.setText(n + "x");
            b.setId(n);
            group.addView(b);
        }
        group.check(now < 1 || now > 3 ? 1 : now);
        group.setOnCheckedChangeListener((g, id) -> writeLine("resolution", String.valueOf(id)));
        box.addView(group);
        return box;
    }

    private Button button(int label, Runnable action) {
        Button b = new Button(this);
        b.setText(label);
        b.setOnClickListener(v -> action.run());
        b.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return b;
    }

    private void toast(int text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    private String status() {
        return text("status.txt", null, 40);
    }

    /** How the run before this one ended, if it crashed: the game's own lines about it
     *  (native_os_portable.c) and an exception of the app's Java side (App.java). */
    private String crashLines() {
        String lines = text("log-1.txt", "crash:", 24);
        /* an exception of the last day: the file stays until the next one replaces it */
        File javaFile = new File(getFilesDir(), App.CRASH_FILE);
        boolean recent = System.currentTimeMillis() - javaFile.lastModified() < 24L * 60 * 60 * 1000;
        String java = recent ? text(App.CRASH_FILE, null, 12) : "";
        return (lines + (java.isEmpty() ? "" : "\n" + java)).trim();
    }

    /** A file of the app's as text: its lines starting with `only`, if given, and no more than
     *  `most` of them, the last ones. */
    private String text(String name, String only, int most) {
        List<String> lines = new ArrayList<>();
        try (BufferedReader in = new BufferedReader(new FileReader(new File(getFilesDir(), name)))) {
            for (String line; (line = in.readLine()) != null; ) {
                if (only == null || line.startsWith(only)) {
                    lines.add(line);
                }
            }
        } catch (IOException e) {
            /* none yet */
        }
        StringBuilder text = new StringBuilder();
        for (int i = Math.max(0, lines.size() - most); i < lines.size(); i++) {
            text.append(lines.get(i)).append('\n');
        }
        return text.toString().trim();
    }

    /** Everything the app has written about its runs, as one text file for a report. */
    private void exportLog(Uri uri) {
        try (OutputStream out = getContentResolver().openOutputStream(uri)) {
            for (String name : new String[] {"status.txt", App.CRASH_FILE, "log.txt", "log-1.txt"}) {
                File file = new File(getFilesDir(), name);
                out.write(("===== " + name + " =====\n").getBytes("UTF-8"));
                if (file.isFile()) {
                    try (InputStream in = new FileInputStream(file)) {
                        copy(in, out);
                    }
                }
                out.write('\n');
            }
            toast(R.string.settings_log_exported);
        } catch (IOException | SecurityException | NullPointerException e) {
            toast(R.string.settings_failed);
        }
    }

    private List<String> lines() {
        List<String> lines = new ArrayList<>();
        try (BufferedReader in = new BufferedReader(new FileReader(settingsFile()))) {
            for (String line; (line = in.readLine()) != null; ) {
                lines.add(line);
            }
        } catch (IOException e) {
            /* none yet */
        }
        return lines;
    }

    private boolean read(String key, boolean byDefault) {
        for (String line : lines()) {
            if (line.startsWith(key + "=")) {
                return !line.substring(key.length() + 1).trim().equals("0");
            }
        }
        return byDefault;
    }

    private int readNumber(String key, int byDefault) {
        for (String line : lines()) {
            if (line.startsWith(key + "=")) {
                try {
                    return Integer.parseInt(line.substring(key.length() + 1).trim());
                } catch (NumberFormatException e) {
                    return byDefault;
                }
            }
        }
        return byDefault;
    }

    private void write(String key, boolean on) {
        writeLine(key, on ? "1" : "0");
    }

    private void writeLine(String key, String value) {
        List<String> lines = lines();
        String line = key + "=" + value;
        boolean found = false;
        for (int i = 0; i < lines.size(); i++) {
            if (lines.get(i).startsWith(key + "=")) {
                lines.set(i, line);
                found = true;
            }
        }
        if (!found) {
            lines.add(line);
        }
        try (FileWriter out = new FileWriter(settingsFile())) {
            for (String l : lines) {
                out.write(l + "\n");
            }
        } catch (IOException e) {
            toast(R.string.settings_failed);
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri uri = data.getData();
        if (request == EXPORT_LOG) {
            exportLog(uri);
        } else if (request == EXPORT_SAVE) {
            try (InputStream in = new FileInputStream(saveFile());
                 OutputStream out = getContentResolver().openOutputStream(uri)) {
                copy(in, out);
                toast(R.string.settings_save_exported);
            } catch (IOException | SecurityException | NullPointerException e) {
                toast(R.string.settings_failed);
            }
        } else if (request == IMPORT_SAVE) {
            /* a save is a 256 KB image; anything else is not put in its place */
            File part = new File(getFilesDir(), "acww.sav.part");
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(part)) {
                copy(in, out);
            } catch (IOException | SecurityException | NullPointerException e) {
                part.delete();
                toast(R.string.settings_failed);
                return;
            }
            if (part.length() != 0x40000) {
                part.delete();
                toast(R.string.settings_save_wrong);
                return;
            }
            File save = saveFile();
            if (save.isFile()) {
                save.renameTo(new File(getFilesDir(), "acww.sav.before-import"));
            }
            if (part.renameTo(save)) {
                closeApp();
            } else {
                toast(R.string.settings_failed);
            }
        }
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) {
            out.write(buf, 0, n);
        }
    }

    /** The whole app, the game included: it starts again with the new save or asks for a ROM. */
    private void closeApp() {
        toast(R.string.settings_restart);
        finishAffinity();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
