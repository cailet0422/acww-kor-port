package io.github.cailet0422.acwwkor;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/** New versions of the app: the project's releases on GitHub.
 *
 *  When the app starts (at most every six hours, and unless "auto_update=0" is in
 *  files/settings.txt), GitHub is asked for the latest release. Nothing of the player or the
 *  device goes with the question. A release newer than the app is offered before the game the
 *  next time the app starts: the release's APK is fetched, checked (the same app, a higher
 *  version, signed with the key this app is signed with) and handed to the system's installer.
 *  The system asks the player before it installs; from Android 12 on, once the app has
 *  installed an update of itself, it lets later ones through without asking again (the
 *  player has said yes in the app's own dialog by then). The game's files stay as they are.
 *
 *  docs/releases.md. */
final class Updater {
    static final String LATEST = "https://api.github.com/repos/cailet0422/acww-kor-port/releases/latest";
    static final String INSTALL_STATUS = "io.github.cailet0422.acwwkor.INSTALL_STATUS";
    private static final String PREFS = "update";
    private static final long EVERY = 6L * 60 * 60 * 1000;
    private static final long APK_MOST = 96L << 20;

    static final class Release {
        String version = ""; /* "0.13.0" */
        String notes = "";
        String url = "";     /* its APK */
        long size;
    }

    private Updater() {
    }

    /* Versions --------------------------------------------------------------------------------- */

    static String installed(Context context) {
        try {
            return context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "0";
        }
    }

    /** Dotted numbers compared part by part ("v0.9.0" is older than "0.12.1"); a part that is
     *  not a number counts as 0. */
    static int compare(String a, String b) {
        String[] x = a.replaceFirst("^[vV]", "").split("[.-]"), y = b.replaceFirst("^[vV]", "").split("[.-]");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int p = i < x.length ? number(x[i]) : 0, q = i < y.length ? number(y[i]) : 0;
            if (p != q) {
                return p < q ? -1 : 1;
            }
        }
        return 0;
    }

    private static int number(String part) {
        try {
            return Integer.parseInt(part.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /* Asking GitHub ---------------------------------------------------------------------------- */

    /** The latest release, or null when the project has none with an APK. Uses the network:
     *  not for the main thread. */
    static Release latest(Context context) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(LATEST).openConnection();
        connection.setConnectTimeout(8000);
        connection.setReadTimeout(8000);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "acww-kor-port/" + installed(context));
        try {
            if (connection.getResponseCode() == 404) {
                return null; /* no release yet */
            }
            if (connection.getResponseCode() != 200) {
                throw new IOException("HTTP " + connection.getResponseCode());
            }
            ByteArrayOutputStream text = new ByteArrayOutputStream();
            try (InputStream in = connection.getInputStream()) {
                copy(in, text, 1 << 20, null);
            }
            return parse(text.toString("UTF-8"));
        } catch (JSONException e) {
            throw new IOException(e);
        } finally {
            connection.disconnect();
        }
    }

    /** A release as GitHub's API describes it: its tag is the version, its first .apk asset
     *  the app. */
    static Release parse(String json) throws JSONException {
        JSONObject object = new JSONObject(json);
        if (object.optBoolean("draft") || object.optBoolean("prerelease")) {
            return null;
        }
        Release release = new Release();
        release.version = object.getString("tag_name").replaceFirst("^[vV]", "");
        release.notes = object.optString("body", "");
        JSONArray assets = object.optJSONArray("assets");
        for (int i = 0; assets != null && i < assets.length(); i++) {
            JSONObject asset = assets.getJSONObject(i);
            String url = asset.optString("browser_download_url", "");
            if (asset.optString("name", "").endsWith(".apk") && url.startsWith("https://github.com/cailet0422/acww-kor-port/")) {
                release.url = url;
                release.size = asset.optLong("size");
                return release;
            }
        }
        return null;
    }

    /* What is known between starts -------------------------------------------------------------- */

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** "auto_update=0" in files/settings.txt (the settings screen's switch) turns the asking
     *  at the start off; asking from the settings screen always works. */
    static boolean auto(Context context) {
        try (BufferedReader in = new BufferedReader(new FileReader(new File(context.getFilesDir(), "settings.txt")))) {
            for (String line; (line = in.readLine()) != null; ) {
                if (line.trim().equals("auto_update=0")) {
                    return false;
                }
            }
        } catch (IOException e) {
            /* no settings yet: on */
        }
        return true;
    }

    static void remember(Context context, Release release) {
        SharedPreferences.Editor editor = prefs(context).edit().putLong("checked", System.currentTimeMillis());
        if (release != null) {
            editor.putString("version", release.version).putString("notes", release.notes).putString("url", release.url)
                    .putLong("size", release.size);
        }
        editor.apply();
    }

    /** The release found by an earlier asking, if it is newer than the app and the player has
     *  not said to leave that version out; or null. */
    static Release known(Context context) {
        SharedPreferences p = prefs(context);
        Release release = new Release();
        release.version = p.getString("version", "");
        release.notes = p.getString("notes", "");
        release.url = p.getString("url", "");
        release.size = p.getLong("size", 0);
        if (release.version.isEmpty() || release.url.isEmpty() || compare(release.version, installed(context)) <= 0 ||
                release.version.equals(p.getString("skipped", ""))) {
            return null;
        }
        return release;
    }

    /** Asks GitHub on a thread of its own, if it is time to, and keeps the answer for the next
     *  start. Says nothing: without a network there is nothing to say. */
    static void checkInBackground(Context context) {
        final Context app = context.getApplicationContext();
        if (!auto(app) || System.currentTimeMillis() - prefs(app).getLong("checked", 0) < EVERY) {
            return;
        }
        new Thread(() -> {
            try {
                remember(app, latest(app));
            } catch (IOException | RuntimeException e) {
                /* next time */
            }
        }, "update-check").start();
    }

    /* Offering and installing ------------------------------------------------------------------- */

    /** The dialog about a newer release. `then` runs when the player goes on without it (or
     *  when the update could not be made); it does not run once the installer has the update. */
    static void offer(final Activity activity, final Release release, final Runnable then) {
        String notes = plain(release.notes, "ko".equals(Locale.getDefault().getLanguage()));
        if (notes.length() > 1200) {
            notes = notes.substring(0, 1200) + "…";
        }
        new AlertDialog.Builder(activity)
                .setTitle(activity.getString(R.string.update_title, release.version))
                .setMessage(activity.getString(R.string.update_message, installed(activity)) + (notes.isEmpty() ? "" : "\n\n" + notes))
                .setCancelable(false)
                .setPositiveButton(R.string.update_now, (d, w) -> install(activity, release, then))
                .setNegativeButton(R.string.update_later, (d, w) -> then.run())
                .setNeutralButton(R.string.update_skip, (d, w) -> {
                    prefs(activity).edit().putString("skipped", release.version).apply();
                    then.run();
                })
                .show();
    }

    /** A release's notes as plain text for a dialog. They are the Korean notes, a line of
     *  dashes, then the English ones (port/tools/release_notes.py), written as Markdown in
     *  lines of a fixed width: the part in the reader's language, its paragraphs' lines
     *  joined and the marks for bold and code taken out. */
    static String plain(String notes, boolean korean) {
        String[] parts = notes.replace("\r", "").split("\n-{3,}\n", 2);
        String part = parts.length == 2 && !korean ? parts[1] : parts[0];
        StringBuilder out = new StringBuilder();
        for (String line : part.trim().split("\n")) {
            String text = line.trim().replace("**", "").replace("`", "");
            if (text.isEmpty()) {
                out.append("\n\n");
            } else if (text.startsWith("- ")) {
                out.append(out.length() == 0 || out.charAt(out.length() - 1) == '\n' ? "" : "\n").append("• ").append(text.substring(2));
            } else {
                out.append(out.length() == 0 || out.charAt(out.length() - 1) == '\n' ? "" : " ").append(text);
            }
        }
        return out.toString().trim();
    }

    /** Fetches the release's APK, checks it and hands it to the system's installer, with a
     *  dialog that says how far it is and lets the player go on without it. */
    static void install(final Activity activity, final Release release, final Runnable then) {
        final boolean[] left = {false};
        final AlertDialog progress = new AlertDialog.Builder(activity)
                .setTitle(activity.getString(R.string.update_title, release.version))
                .setMessage(activity.getString(R.string.update_fetching, 0))
                .setCancelable(false)
                .setNegativeButton(R.string.update_later, (d, w) -> {
                    left[0] = true;
                    then.run();
                })
                .show();
        new Thread(() -> {
            final File apk = new File(activity.getCacheDir(), "update.apk");
            int problem = 0;
            try {
                fetch(release, apk, percent -> activity.runOnUiThread(() -> {
                    if (!left[0]) {
                        progress.setMessage(activity.getString(R.string.update_fetching, percent));
                    }
                }), left);
                problem = left[0] ? 0 : check(activity, apk);
                if (problem == 0 && !left[0]) {
                    commit(activity, apk);
                }
            } catch (IOException | RuntimeException e) {
                problem = R.string.update_failed_fetch;
            }
            final int said = problem;
            activity.runOnUiThread(() -> {
                if (left[0]) {
                    return;
                }
                if (said != 0) {
                    apk.delete();
                    progress.dismiss();
                    new AlertDialog.Builder(activity)
                            .setMessage(said)
                            .setCancelable(false)
                            .setPositiveButton(android.R.string.ok, (d, w) -> then.run())
                            .show();
                } else {
                    /* the system's installer takes over from here (UpdateReceiver) */
                    progress.setMessage(activity.getString(R.string.update_installing));
                }
            });
        }, "update-fetch").start();
    }

    private interface Progress {
        void at(int percent);
    }

    private static void fetch(Release release, File to, Progress progress, boolean[] left) throws IOException {
        if (!release.url.startsWith("https://github.com/cailet0422/acww-kor-port/")) {
            throw new IOException("not the project's release");
        }
        HttpURLConnection connection = (HttpURLConnection) new URL(release.url).openConnection();
        connection.setConnectTimeout(10000);
        connection.setReadTimeout(20000);
        connection.setRequestProperty("User-Agent", "acww-kor-port");
        try {
            if (connection.getResponseCode() != 200) {
                throw new IOException("HTTP " + connection.getResponseCode());
            }
            final long whole = release.size > 0 ? release.size : connection.getContentLength();
            try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(to)) {
                copy(in, out, APK_MOST, done -> {
                    if (left[0]) {
                        throw new IllegalStateException("left");
                    }
                    if (whole > 0) {
                        progress.at((int) Math.min(100, done * 100 / whole));
                    }
                });
            }
        } finally {
            connection.disconnect();
        }
    }

    private interface Copied {
        void bytes(long done);
    }

    private static void copy(InputStream in, OutputStream out, long most, Copied copied) throws IOException {
        byte[] buffer = new byte[1 << 16];
        long done = 0, said = 0;
        for (int n; (n = in.read(buffer)) > 0; ) {
            done += n;
            if (done > most) {
                throw new IOException("too large");
            }
            out.write(buffer, 0, n);
            if (copied != null && done - said >= (256 << 10)) {
                said = done;
                copied.bytes(done);
            }
        }
        if (copied != null) {
            copied.bytes(done);
        }
    }

    /** The fetched file is this app, newer, and signed as this app is: 0, or the string that
     *  says what is wrong. (The system checks the signature again as it installs.) */
    @SuppressWarnings("deprecation")
    private static int check(Context context, File apk) {
        PackageManager manager = context.getPackageManager();
        try {
            PackageInfo mine = manager.getPackageInfo(context.getPackageName(), PackageManager.GET_SIGNATURES);
            PackageInfo its = manager.getPackageArchiveInfo(apk.getPath(), PackageManager.GET_SIGNATURES);
            if (its == null || !context.getPackageName().equals(its.packageName)) {
                return R.string.update_failed_file;
            }
            if (its.versionCode <= mine.versionCode) {
                return R.string.update_failed_file;
            }
            Set<String> a = certificates(mine.signatures), b = certificates(its.signatures);
            if (!a.isEmpty() && !b.isEmpty() && !a.equals(b)) {
                return R.string.update_failed_key;
            }
        } catch (PackageManager.NameNotFoundException e) {
            return R.string.update_failed_file;
        }
        return 0;
    }

    private static Set<String> certificates(Signature[] signatures) {
        Set<String> set = new HashSet<>();
        for (int i = 0; signatures != null && i < signatures.length; i++) {
            set.add(Arrays.toString(signatures[i].toByteArray()));
        }
        return set;
    }

    /** The file to the system's installer, as an update of this app. What comes of it is told
     *  to UpdateReceiver. */
    private static void commit(Context context, File apk) throws IOException {
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());
        if (Build.VERSION.SDK_INT >= 31) {
            /* the system then asks only where its own rules say it must (the first time) */
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        }
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("update", 0, apk.length())) {
                copy(in, out, APK_MOST, null);
                session.fsync(out);
            }
            Intent told = new Intent(context, UpdateReceiver.class).setAction(INSTALL_STATUS);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            session.commit(PendingIntent.getBroadcast(context, id, told, flags).getIntentSender());
        } catch (IOException | RuntimeException e) {
            try {
                installer.abandonSession(id);
            } catch (RuntimeException gone) {
                /* it is gone already */
            }
            throw e;
        }
    }
}
