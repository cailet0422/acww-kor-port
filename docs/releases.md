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
2. The tag `v<version>` is pushed on the commit to release.
3. `.github/workflows/release.yml` builds the APK, signs it, checks the signature and
   publishes the release. It can also be started by hand (Actions, "release", "Run
   workflow"). It refuses a tag that is not `port/VERSION`'s, a version that is released
   already, and an APK that is not signed with the project's key.

## The key

Android installs an update only over an app signed with the same key. Every release has to
be signed with the key the first one was, for as long as the app has players who would
rather not uninstall it (which takes the save with it; Settings > Data exports it first).

- The key's certificate has the SHA-256
  `b8ff20ae83f5351ceed5a6def11522f470679c551af1cb41cf036c917e5453f7` (`android/release-key.sha256`);
  the release workflow compares each APK with it, and the app compares a fetched update's
  signature with its own before it hands it to the system.
- The workflow takes the keystore from the repository secret `RELEASE_KEYSTORE_BASE64` (the
  keystore file, base64), or from `android/release.keystore` if the repository has that file.
  Its passwords and alias are `android`, `androiddebugkey`, `android` unless the secrets
  `RELEASE_KEYSTORE_PASSWORD`, `RELEASE_KEY_ALIAS` and `RELEASE_KEY_PASSWORD` say otherwise.
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
