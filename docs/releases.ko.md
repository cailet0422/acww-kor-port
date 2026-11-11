<p align="center"><a href="releases.md">English</a> · <b>한국어</b></p>

# 릴리즈와 업데이트

## 릴리즈란

릴리즈는 프로젝트 [릴리즈 페이지](https://github.com/cailet0422/acww-kor-port/releases)에 올라가는 한 버전의
APK입니다. `acww-kor-port-<버전>.apk`, 그 SHA-256을 담은 `SHA256SUMS`, 버전 설명으로 이루어집니다. 버전은
`port/VERSION`의 것이고(앱의 `versionName`. `versionCode`도 함께 커집니다), 릴리즈의 태그는 `v<버전>`입니다.

## 릴리즈 만들기

1. `port/VERSION`에 새 버전을 적고, `CHANGELOG.md`와 `CHANGELOG.ko.md`에 각각 `## <버전>` 절을 둡니다
   (릴리즈 설명은 이 두 절이며 한국어가 먼저 옵니다: `port/tools/release_notes.py`).
2. 키를 보관하는 사람이 그 커밋에서 APK를 빌드합니다. `android/`에서
   `./gradlew assembleRelease -PreleaseKeystore=파일`.
3. 빌드한 커밋 위에 APK를 `release/acww-kor-port-<버전>.apk`로 담은 커밋을 별도 브랜치에 올리고, 그
   브랜치에서 `.github/workflows/release.yml`을 실행합니다(Actions, "release", "Run workflow"). `target`에는
   빌드한 커밋을 줍니다. 워크플로는 그 파일이 앱의 그 버전이고 프로젝트의 키로 서명됐는지 확인한 뒤 릴리즈를
   게시합니다. APK, 그 SHA-256, 설명, 그리고 target에 붙는 `v<버전>` 태그입니다. APK를 담았던 브랜치는 그 뒤에
   지웁니다. APK는 저장소 이력에 남기지 않습니다.

이미 릴리즈된 버전, 다른 버전의 APK, 프로젝트의 키로 서명되지 않은 APK는 워크플로가 거부합니다.

## 서명 키

Android는 같은 키로 서명된 앱 위에만 업데이트를 설치합니다. 앱을 지우고 싶지 않은 플레이어가 있는 한(지우면
세이브도 사라집니다. 설정 > 데이터에서 먼저 내보낼 수 있습니다) 모든 릴리즈를 첫 릴리즈와 같은 키로 서명해야
합니다.

- 키의 인증서 SHA-256은 `b8ff20ae83f5351ceed5a6def11522f470679c551af1cb41cf036c917e5453f7`입니다
  (`android/release-key.sha256`). 릴리즈 워크플로가 APK마다 이 값과 비교하고, 앱도 내려받은 업데이트의 서명을
  자신의 서명과 비교한 뒤에 시스템에 넘깁니다.
- 키는 저장소에도 GitHub에도 없습니다. 프로젝트 소유자가 키스토어 파일을 보관하고, 릴리즈는 그 파일이
  있는 곳에서 빌드합니다. (비밀번호와 별칭은 Android 디버그 키의 것입니다: `android`, `androiddebugkey`,
  `android`.) 이 파일을 잃으면 이후 릴리즈는 이전 릴리즈 위에 설치되지 않습니다.
- 나중에 키를 GitHub에 두기로 한다면: 키스토어 파일의 base64를 저장소 시크릿 `RELEASE_KEYSTORE_BASE64`에
  넣으면, 넘겨받은 APK가 없을 때 워크플로가 직접 빌드하고 서명합니다(그때는 `v<버전>` 태그만 푸시하면
  됩니다).
- 키 없이 만든 빌드(`./gradlew assembleRelease`, CI의 "build" 워크플로)는 빌드한 기기의 디버그 키로
  서명됩니다. 실행은 되지만 그 기기에서 만든 빌드 위에만 업데이트로 설치됩니다. `-PreleaseKeystore=파일`을
  주면 로컬 빌드도 프로젝트의 키로 서명합니다.

## 앱이 업데이트하는 방법

`android/.../Updater.java`:

- 앱을 시작할 때, 많아야 여섯 시간에 한 번
  `api.github.com/repos/cailet0422/acww-kor-port/releases/latest`에 묻습니다. 요청에는 `User-Agent`에 앱
  버전이 들어갈 뿐 기기나 플레이어에 관한 것은 없습니다. 설정 > 정보의 "새 버전 자동 확인"으로 끌 수 있고
  (`settings.txt`의 `auto_update=0`), 같은 곳의 "지금 업데이트 확인"은 바로 묻습니다.
- 앱보다 새 릴리즈(초안이나 사전 릴리즈 제외)가 있으면 다음에 앱을 시작할 때 게임보다 먼저 알려 줍니다.
  업데이트, 나중에, 이 버전 건너뛰기 가운데 고릅니다. 게임 도중에 끼어들지 않습니다.
- "업데이트"는 프로젝트의 릴리즈에서 APK를 내려받아, 이 앱이 맞는지, 더 높은 버전인지, 설치된 앱과 같은
  키로 서명됐는지 확인하고 시스템 설치기(`PackageInstaller`)에 넘깁니다. 시스템이 허용할지 묻습니다(처음에는
  이 앱의 설치 허용도 묻습니다). Android 12부터는 앱이 이렇게 한 번 스스로 업데이트하고 나면 이후 업데이트는
  시스템이 따로 묻지 않고 통과시킵니다. 앱의 파일(ROM 사본, 세이브, 설정)은 업데이트로 바뀌지 않습니다.
- 설치가 끝나면 시스템이 앱을 닫아 두므로 플레이어가 다시 엽니다.

아직 기기에서 해 보지 못했습니다. 첫 릴리즈가 업데이트할 첫 대상이기 때문입니다.
