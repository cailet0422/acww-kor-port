package io.github.cailet0422.acwwkor;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.TypedArray;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Switch;
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
 *  A page for each kind of thing (the picture, the controls, the game, playing with others,
 *  the files, how the run is going), chosen by the tabs under the title; a setting is a row
 *  with its name, a line on what it does, and a switch. The choices are lines of
 *  files/settings.txt, which the game reads when it starts and again when it comes back to
 *  the front (port/src/front_sdl.c): only the lines this screen knows are rewritten, the rest
 *  (the buttons' arrangement) are kept as they are. What takes effect only at the game's next
 *  start says so once it is changed, with a button that starts the app again. The save can be
 *  copied out to a file and replaced by one, and another ROM chosen; after either of the last
 *  two the app closes, since the running game holds the old ones. */
public class SettingsActivity extends Activity {
    private static final int EXPORT_SAVE = 1;
    private static final int IMPORT_SAVE = 2;
    private static final int EXPORT_LOG = 3;

    private static final int[] TABS = {R.string.tab_display, R.string.tab_controls, R.string.tab_game,
            R.string.tab_online, R.string.tab_data, R.string.tab_about};
    /** the page shown, kept while the app lives: the settings are left and come back to */
    private static int page;
    /** something that waits for the next start was changed since the game started */
    private static boolean restartWanted;

    private float density;
    private int accent, secondary;
    private LinearLayout content, restartBar;
    private final TextView[] tabs = new TextView[TABS.length];
    private final View[] tabLines = new View[TABS.length];

    private File settingsFile() {
        return new File(getFilesDir(), "settings.txt");
    }

    private File saveFile() {
        return new File(getFilesDir(), "acww.sav");
    }

    private int dp(float n) {
        return (int) (n * density + 0.5f);
    }

    /** A colour of the theme's (through the theme: some are lists that name others of its). */
    private int themeColor(int attr, int otherwise) {
        TypedArray values = obtainStyledAttributes(new int[] {attr});
        try {
            return values.getColor(0, otherwise);
        } catch (RuntimeException e) {
            return otherwise;
        } finally {
            values.recycle();
        }
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        density = getResources().getDisplayMetrics().density;
        accent = themeColor(android.R.attr.colorAccent, 0xff80cbc4);
        secondary = themeColor(android.R.attr.textColorSecondary, 0xffb0b0b0);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        /* the title, and the way back to the game beside it */
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(20), dp(12), dp(12), dp(4));
        TextView title = new TextView(this);
        title.setText(R.string.settings_title);
        title.setTextSize(22);
        bar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        bar.addView(button(R.string.settings_back, this::finish));
        root.addView(bar);

        /* the tabs */
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(dp(8), 0, dp(8), 0);
        for (int i = 0; i < TABS.length; i++) {
            final int at = i;
            LinearLayout tab = new LinearLayout(this);
            tab.setOrientation(LinearLayout.VERTICAL);
            tab.setGravity(Gravity.CENTER_HORIZONTAL);
            tabs[i] = new TextView(this);
            tabs[i].setText(TABS[i]);
            tabs[i].setTextSize(15);
            tabs[i].setPadding(dp(14), dp(10), dp(14), dp(8));
            tab.addView(tabs[i]);
            tabLines[i] = new View(this);
            tab.addView(tabLines[i], new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(2)));
            tab.setOnClickListener(v -> show(at));
            row.addView(tab);
        }
        HorizontalScrollView tabScroll = new HorizontalScrollView(this);
        tabScroll.setHorizontalScrollBarEnabled(false);
        tabScroll.addView(row);
        root.addView(tabScroll);
        root.addView(divider(0));

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(8), dp(20), dp(24));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        /* "at the next start", once something of that kind was changed */
        restartBar = new LinearLayout(this);
        restartBar.setOrientation(LinearLayout.HORIZONTAL);
        restartBar.setGravity(Gravity.CENTER_VERTICAL);
        restartBar.setPadding(dp(20), dp(6), dp(12), dp(6));
        TextView restartText = new TextView(this);
        restartText.setText(R.string.settings_restart_needed);
        restartText.setTextSize(13);
        restartBar.addView(restartText, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        restartBar.addView(button(R.string.settings_restart_now, this::restartApp));
        restartBar.setVisibility(restartWanted ? View.VISIBLE : View.GONE);
        root.addView(restartBar);

        setContentView(root);
        page = getIntent().getIntExtra("page", page); /* (a page asked for by number: for a check) */
        show(page < 0 || page >= TABS.length ? 0 : page);
    }

    /** The page of tab `at` in place of the one shown. */
    private void show(int at) {
        page = at;
        for (int i = 0; i < TABS.length; i++) {
            boolean on = i == at;
            tabs[i].setTextColor(on ? accent : secondary);
            tabs[i].setTypeface(null, on ? Typeface.BOLD : Typeface.NORMAL);
            tabLines[i].setBackgroundColor(on ? accent : 0);
        }
        content.removeAllViews();
        switch (at) {
            case 0:
                displayPage();
                break;
            case 1:
                controlsPage();
                break;
            case 2:
                gamePage();
                break;
            case 3:
                onlinePage();
                break;
            case 4:
                dataPage();
                break;
            default:
                aboutPage();
                break;
        }
    }

    /* The pages -------------------------------------------------------------------------------- */

    private void displayPage() {
        final LinearLayout[] size = new LinearLayout[3];
        final TextView[] turnedOff = new TextView[1];
        content.addView(toggle("vulkan", true, R.string.settings_vulkan, R.string.settings_vulkan_about, true, on -> {
            dim(size[0], on);
            dim(size[1], !on);
            dim(size[2], !on);
            if (on) {
                /* turned on again by hand: the game's word on why it turned it off goes */
                removeLine("vulkan_off");
                if (turnedOff[0] != null) {
                    turnedOff[0].setVisibility(View.GONE);
                }
            }
        }));
        if (!read("vulkan", true) && !readText("vulkan_off").isEmpty()) {
            turnedOff[0] = note(getString(R.string.settings_vulkan_off));
            turnedOff[0].setTextColor(accent);
            content.addView(turnedOff[0]);
        }
        String now = statusLine("picture:");
        String why = statusLine("vulkan:");
        if (!now.isEmpty()) {
            content.addView(note(getString(R.string.settings_now) + " " + now + (why.isEmpty() ? "" : "\n" + why)));
        }
        size[2] = toggle("fps60", true, R.string.settings_fps60, R.string.settings_fps60_about, false, null);
        content.addView(size[2]);
        size[1] = pick("vulkan_scale", 100, new int[] {100, 75, 50}, "%", R.string.settings_vulkan_scale,
                R.string.settings_vulkan_scale_about, false);
        content.addView(size[1]);
        content.addView(divider(8));
        content.addView(toggle("tall", true, R.string.settings_tall, R.string.settings_tall_about, true, null));
        content.addView(divider(8));
        size[0] = pick("resolution", 1, new int[] {1, 2, 3}, "x", R.string.settings_resolution,
                R.string.settings_resolution_about, true);
        content.addView(size[0]);
        dim(size[0], read("vulkan", true));
        dim(size[1], !read("vulkan", true));
        dim(size[2], !read("vulkan", true));
        content.addView(divider(8));
        content.addView(toggle("fast_cores", true, R.string.settings_fast_cores, R.string.settings_fast_cores_about, true, null));
        String cores = statusLine("cores:");
        if (!cores.isEmpty()) {
            content.addView(note(getString(R.string.settings_now) + " " + cores));
        }
    }

    private void controlsPage() {
        content.addView(toggle("type_keyboard", true, R.string.settings_type_keyboard, R.string.settings_type_keyboard_about, false, null));
        content.addView(divider(8));
        content.addView(toggle("full_touch", false, R.string.settings_full_touch, R.string.settings_full_touch_about, false, null));
        content.addView(divider(8));
        content.addView(heading(R.string.settings_arrange_title));
        content.addView(note(getString(R.string.settings_arrange_about)));
        /* a request the game takes when it is back in front: the arranging opens */
        content.addView(button(R.string.settings_arrange, () -> {
            write("full_touch", false);
            write("arrange", true);
            finish();
        }));
    }

    private void gamePage() {
        content.addView(toggle("autosave", true, R.string.settings_autosave, R.string.settings_autosave_about, false, null));
        content.addView(divider(8));
        content.addView(toggle("resetti", false, R.string.settings_resetti, R.string.settings_resetti_about, false, null));
        content.addView(divider(8));
        content.addView(toggle("sound", true, R.string.settings_sound, R.string.settings_sound_about, true, null));
    }

    private void onlinePage() {
        /* playing with others (docs/multiplayer.md): read when the game starts */
        content.addView(toggle("link", false, R.string.settings_link, R.string.settings_link_about, true, null));
        content.addView(field("link_server", R.string.settings_link_server));
        content.addView(field("link_room", R.string.settings_link_room));
        content.addView(more(R.string.settings_link_note));
        String link = statusLine("link:");
        if (!link.isEmpty()) {
            content.addView(note(getString(R.string.settings_now) + " " + link));
        }
        content.addView(divider(12));
        content.addView(toggle("wifi", false, R.string.settings_wifi, R.string.settings_wifi_about, true, null));
        content.addView(field("wifi_dns", R.string.settings_wifi_dns));
        content.addView(more(R.string.settings_wifi_note));
        String wifi = statusLine("wifi:");
        if (!wifi.isEmpty()) {
            content.addView(note(getString(R.string.settings_now) + " " + wifi));
        }
    }

    private void dataPage() {
        content.addView(heading(R.string.settings_save_title));
        content.addView(note(getString(R.string.settings_save_about)));
        LinearLayout pair = new LinearLayout(this);
        pair.setOrientation(LinearLayout.HORIZONTAL);
        pair.addView(button(R.string.settings_save_export, () -> {
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
        pair.addView(button(R.string.settings_save_import, () -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, IMPORT_SAVE);
        }));
        content.addView(pair);
        content.addView(divider(12));
        content.addView(heading(R.string.settings_rom_title));
        content.addView(note(getString(R.string.settings_rom_change_ask)));
        content.addView(button(R.string.settings_rom_change, () -> new AlertDialog.Builder(this)
                .setMessage(R.string.settings_rom_change_ask)
                .setPositiveButton(android.R.string.ok, (d, w) -> {
                    new File(getFilesDir(), "game.nds").delete();
                    closeApp();
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show()));
    }

    private void aboutPage() {
        String version = "";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            /* its own package: always there */
        }
        content.addView(heading(getString(R.string.app_name) + " " + version));
        /* updates: the project's releases (Updater.java) */
        content.addView(toggle("auto_update", true, R.string.settings_auto_update, R.string.settings_auto_update_about, false, null));
        final Button[] check = new Button[1];
        check[0] = button(R.string.settings_update_check, () -> {
            check[0].setEnabled(false);
            new Thread(() -> {
                Updater.Release found = null;
                boolean asked = true;
                try {
                    found = Updater.latest(this);
                    Updater.remember(this, found);
                } catch (IOException | RuntimeException e) {
                    asked = false;
                }
                final Updater.Release release = found;
                final boolean answered = asked;
                runOnUiThread(() -> {
                    check[0].setEnabled(true);
                    if (!answered) {
                        toast(R.string.settings_update_unknown);
                    } else if (release == null || Updater.compare(release.version, Updater.installed(this)) <= 0) {
                        toast(R.string.settings_update_none);
                    } else {
                        Updater.offer(this, release, () -> { });
                    }
                });
            }, "update-check").start();
        });
        content.addView(check[0]);
        content.addView(divider(8));
        /* how the run is going (front_sdl.c, status_write): something to read out in a report */
        String status = status();
        String crash = crashLines();
        content.addView(note(getString(R.string.settings_status)));
        if (!status.isEmpty()) {
            TextView state = note(status);
            state.setTextIsSelectable(true);
            state.setTypeface(Typeface.MONOSPACE);
            state.setTextSize(12);
            content.addView(state);
        }
        if (!crash.isEmpty()) {
            content.addView(divider(8));
            content.addView(heading(R.string.settings_crashed));
            TextView lines = note(crash);
            lines.setTextIsSelectable(true);
            lines.setTypeface(Typeface.MONOSPACE);
            lines.setTextSize(12);
            content.addView(lines);
        }
        content.addView(divider(8));
        content.addView(button(R.string.settings_log_export, () -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TITLE, "acww-kor-port-log.txt");
            startActivityForResult(intent, EXPORT_LOG);
        }));
    }

    /* The parts of a page ---------------------------------------------------------------------- */

    private interface Changed {
        void to(boolean on);
    }

    /** A setting that is on or off: its name, a line about it, and the switch. `atStart`: it
     *  takes effect when the game next starts. */
    private LinearLayout toggle(String key, boolean byDefault, int name, int about, boolean atStart, Changed then) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(10), 0, dp(10));
        LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText(name);
        title.setTextSize(16);
        words.addView(title);
        TextView line = new TextView(this);
        line.setText(getString(about) + (atStart ? " " + getString(R.string.settings_at_start) : ""));
        line.setTextSize(13);
        line.setTextColor(secondary);
        words.addView(line);
        row.addView(words, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Switch box = new Switch(this);
        box.setChecked(read(key, byDefault));
        box.setPadding(dp(12), 0, 0, 0);
        box.setOnCheckedChangeListener((b, on) -> {
            write(key, on);
            if (atStart) {
                wantRestart();
            }
            if (then != null) {
                then.to(on);
            }
        });
        row.addView(box);
        row.setOnClickListener(v -> box.toggle());
        return row;
    }

    private TextView heading(int text) {
        return heading(getString(text));
    }

    private TextView heading(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(16);
        view.setPadding(0, dp(10), 0, dp(2));
        return view;
    }

    private TextView note(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(13);
        view.setTextColor(secondary);
        view.setPadding(0, dp(2), 0, dp(6));
        return view;
    }

    /** A longer explanation, folded away under "how to use it". */
    private LinearLayout more(int text) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        TextView open = new TextView(this);
        open.setText(R.string.settings_more);
        open.setTextSize(13);
        open.setTextColor(accent);
        open.setPadding(0, dp(6), 0, dp(6));
        TextView words = note(getString(text));
        words.setVisibility(View.GONE);
        open.setOnClickListener(v -> words.setVisibility(words.getVisibility() == View.GONE ? View.VISIBLE : View.GONE));
        box.addView(open);
        box.addView(words);
        return box;
    }

    private View divider(int space) {
        LinearLayout box = new LinearLayout(this);
        box.setPadding(0, dp(space), 0, dp(space));
        View line = new View(this);
        line.setBackgroundColor(0x30808080);
        box.addView(line, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(1))));
        return box;
    }

    /** Greyed and untouchable while it does not apply. */
    private void dim(LinearLayout part, boolean dimmed) {
        if (part == null) {
            return;
        }
        part.setAlpha(dimmed ? 0.4f : 1f);
        if (part.hasOnClickListeners()) {
            part.setClickable(!dimmed); /* (a row that is touched as a whole) */
        }
        for (int i = 0; i < part.getChildCount(); i++) {
            View child = part.getChildAt(i);
            child.setEnabled(!dimmed);
            if (child instanceof RadioGroup) {
                RadioGroup group = (RadioGroup) child;
                for (int j = 0; j < group.getChildCount(); j++) {
                    group.getChildAt(j).setEnabled(!dimmed);
                }
            }
        }
    }

    private void wantRestart() {
        restartWanted = true;
        restartBar.setVisibility(View.VISIBLE);
    }

    /** A setting that is one of a few numbers: its name, a line about it, and the choices. */
    private LinearLayout pick(String key, int byDefault, int[] values, String unit, int name, int aboutText, boolean atStart) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, dp(10), 0, dp(10));
        TextView label = new TextView(this);
        label.setText(name);
        label.setTextSize(16);
        box.addView(label);
        TextView about = new TextView(this);
        about.setText(getString(aboutText) + (atStart ? " " + getString(R.string.settings_at_start) : ""));
        about.setTextSize(13);
        about.setTextColor(secondary);
        box.addView(about);
        RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.HORIZONTAL);
        int now = readNumber(key, byDefault), chosen = byDefault;
        for (int value : values) {
            RadioButton b = new RadioButton(this);
            b.setText(value + unit);
            b.setId(value);
            b.setPadding(0, 0, dp(16), 0);
            group.addView(b);
            if (value == now) {
                chosen = value;
            }
        }
        group.check(chosen);
        group.setOnCheckedChangeListener((g, id) -> {
            writeLine(key, String.valueOf(id));
            if (atStart) {
                wantRestart();
            }
        });
        box.addView(group);
        return box;
    }

    /** A line of text kept under `key` as it is typed: read when the game starts. */
    private EditText field(String key, int hint) {
        EditText text = new EditText(this);
        text.setHint(hint);
        text.setTextSize(15);
        text.setSingleLine(true);
        text.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        text.setText(readText(key));
        text.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {}

            @Override
            public void afterTextChanged(Editable s) {
                writeLine(key, s.toString().replace("\n", "").trim());
                wantRestart();
            }
        });
        return text;
    }

    private String readText(String key) {
        for (String line : lines()) {
            if (line.startsWith(key + "=")) {
                return line.substring(key.length() + 1);
            }
        }
        return "";
    }

    private Button button(int label, Runnable action) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setOnClickListener(v -> action.run());
        b.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return b;
    }

    private void toast(int text) {
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
    }

    /** A line of the run's status (status.txt) that starts with `name`, without the name. */
    private String statusLine(String name) {
        String line = text("status.txt", name, 1);
        return line.isEmpty() ? "" : line.substring(name.length()).trim();
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
        writeLines(lines);
    }

    private void removeLine(String key) {
        List<String> lines = lines(), kept = new ArrayList<>();
        for (String l : lines) {
            if (!l.startsWith(key + "=")) {
                kept.add(l);
            }
        }
        if (kept.size() != lines.size()) {
            writeLines(kept);
        }
    }

    /** The settings' file written whole: beside it first, then in its place (the game reads
     *  and writes the same file, port/src/front_sdl.c). */
    private void writeLines(List<String> lines) {
        File part = new File(getFilesDir(), "settings.txt.part");
        try (FileWriter out = new FileWriter(part)) {
            for (String l : lines) {
                out.write(l + "\n");
            }
        } catch (IOException e) {
            toast(R.string.settings_failed);
            return;
        }
        if (!part.renameTo(settingsFile())) {
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
                /* the port's own save was of the town this one replaces */
                new File(getFilesDir(), "acww.sav.auto").delete();
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

    /** The app from its start again, for what the game reads only then (its save was written
     *  when the settings came to the front). */
    private void restartApp() {
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            finishAffinity();
            startActivity(launch);
        } else {
            finishAffinity();
        }
        Runtime.getRuntime().exit(0);
    }

    /** The whole app, the game included: it starts again with the new save or asks for a ROM. */
    private void closeApp() {
        toast(R.string.settings_restart);
        finishAffinity();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
