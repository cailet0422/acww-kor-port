package io.github.cailet0422.acwwkor;

import android.app.Application;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Date;

/** The app's process: an exception nothing caught is written to files/java-crash.txt before the
 *  system ends the app as it would have, so that the settings screen can show it and the log
 *  export carry it (SettingsActivity). The game's own code reports its crashes to
 *  files/log.txt (port/src/native_os_portable.c). */
public class App extends Application {
    static final String CRASH_FILE = "java-crash.txt";

    @Override
    public void onCreate() {
        super.onCreate();
        final Thread.UncaughtExceptionHandler before = Thread.getDefaultUncaughtExceptionHandler();
        final File file = new File(getFilesDir(), CRASH_FILE);
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try (PrintWriter out = new PrintWriter(new FileWriter(file))) {
                out.println(new Date() + ", thread " + thread.getName());
                error.printStackTrace(out);
            } catch (Exception e) {
                /* nothing more to do about it */
            }
            if (before != null) {
                before.uncaughtException(thread, error);
            }
        });
    }
}
