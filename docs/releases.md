<p align="center"><b>English</b> · <a href="releases.ko.md">한국어</a></p>

# Releases and updates

## What a release is

A release is one version's APK on the project's
[releases page](https://github.com/cailet0422/acww-kor-port/releases): `acww-kor-port-<version>.apk`,
its SHA-256 in `SHA256SUMS`, and the version's notes. The version is the one in `port/VERSION`
(the app's `versionName`; its `versionCode` grows with it), and the release's tag is
`v<version>`.

## Making one

1. `port/VERSION` has the new version, and `CHANGELOG.md` and `CHANGELOG.ko.md` each have a
   section `## <version>` (the release's notes are those two sections, the Korean one first:
   `port/tools/release_notes.py`).
2. Whoever keeps the key builds the APK from that commit:
   `./gradlew assembleRelease -PreleaseKeystore=FILE` (in `android/`).
3. The APK is handed to `.github/workflows/release.yml` as
   `release/acww-kor-port-<version>.apk` in a commit on a branch of its own, on top of the
   commit it was built from, and the workflow is run on that branch (Actions, "release",
   "Run workflow") with the built commit as its `target`. The workflow checks that the file
   is that version of the app and signed with the project's key, and publishes the release:
   the APK, its SHA-256, the notes, and the tag `v<version>` on the target. The branch with
   the APK is then dropped: the APK is not kept in the repository's history.

The workflow refuses a version that is released already, an APK of another version, and
one that is not signed with the project's key.

## The key

Android installs an update only over an app signed with the same key. Every release has to
be signed with the key the first one was, for as long as the app has players who would
rather not uninstall it (which takes the save with it; Settings > Data exports it first).

- The key's certificate has the SHA-256
  `b8ff20ae83f5351ceed5a6def11522f470679c551af1cb41cf036c917e5453f7` (`android/release-key.sha256`);
  the release workflow compares each APK with it, and the app compares a fetched update's
  signature with its own before it hands it to the system.
- The key is not in the repository and not on GitHub: the project's owner keeps the
  keystore file, and a release is built where that file is. (Its passwords and alias are
  the Android debug key's: `android`, `androiddebugkey`, `android`.) If the file is lost,
  no later release installs over an earlier one.
- Should the key be put on GitHub after all: with the keystore file, base64, in the
  repository secret `RELEASE_KEYSTORE_BASE64`, the workflow builds and signs the APK itself
  when none is handed in (pushing the tag `v<version>` is then enough).
- A build made without it (`./gradlew assembleRelease`, and CI's "build" workflow) is signed
  with the building machine's debug key: it runs, and installs as an update only over builds
  from that machine. `-PreleaseKeystore=FILE` signs a local build with the project's key.

## How the app updates

`android/.../Updater.java`:

- When the app starts, at most every six hours, it asks
  `api.github.com/repos/cailet0422/acww-kor-port/releases/latest`. The request carries the
  app's version in its `User-Agent` and nothing else of the device or the player. "Look for
  new versions" under Settings > About turns this off (`auto_update=0` in `settings.txt`);
  "Check for an update now" there asks at once.
- A release newer than the app (and not a draft or a prerelease) is offered before the game
  the next time the app starts: update, later, or skip this version. The game is never
  interrupted for it.
- "Update" fetches the release's APK from the project's own releases, checks that it is
  this app, a higher version and signed as the installed app is, and hands it to the
  system's installer (`PackageInstaller`). The system asks the player to allow it (the first
  time also to allow installs from this app); from Android 12 on, once the app has updated
  itself this way, the system lets later updates through without its own question. The
  app's files (the ROM copy, the save, the settings) are not touched by an update.
- After the install the system has closed the app; the player opens it again.

Not tried on a device yet: the first release is the first thing there is to update to.
