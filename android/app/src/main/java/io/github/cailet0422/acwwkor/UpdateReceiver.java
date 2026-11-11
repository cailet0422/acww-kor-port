package io.github.cailet0422.acwwkor;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.widget.Toast;

import java.io.File;

/** What the system's installer says about an update handed to it (Updater.commit): when it
 *  wants the player's word first, its own question is shown; when the update is not made,
 *  why. After an update that is made there is nothing to say: the system has ended the app
 *  to replace it, and the player opens the new one. */
public class UpdateReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Updater.INSTALL_STATUS.equals(intent.getAction())) {
            return;
        }
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent ask = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (ask != null) {
                try {
                    context.startActivity(ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                } catch (RuntimeException e) {
                    Toast.makeText(context, R.string.update_failed_install, Toast.LENGTH_LONG).show();
                }
            }
            return;
        }
        new File(context.getCacheDir(), "update.apk").delete();
        if (status != PackageInstaller.STATUS_SUCCESS) {
            String why = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            int said = status == PackageInstaller.STATUS_FAILURE_ABORTED ? R.string.update_not_installed : R.string.update_failed_install;
            Toast.makeText(context, context.getString(said) + (why == null || why.isEmpty() ? "" : "\n" + why), Toast.LENGTH_LONG).show();
        }
    }
}
