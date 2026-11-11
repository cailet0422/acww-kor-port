<p align="center"><a href="building.md">English</a> · <b>한국어</b></p>

# 빌드하기

디컴프와 함께 클론합니다.

```sh
git clone --recursive https://github.com/cailet0422/acww-kor-port
# 서브모듈 없이 클론했다면:
git submodule update --init
```

빌드에는 ROM이 필요 없습니다. ROM은 프로그램을 실행할 때 요청합니다.

## Linux

Ubuntu 24.04 기준:

```sh
sudo apt-get install build-essential cmake ninja-build libsdl2-dev
cmake -S port -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/acww-kor --rom 경로/한국판.nds
```

옵션은 `build/acww-kor --help`로 볼 수 있습니다. `-DSDL_SOURCE_DIR=/SDL/경로`를 주면 시스템 SDL2 대신
소스 트리에서 SDL2를 빌드하고, `-DPORT_FRONT_END=OFF`는 SDL2가 필요 없는 테스트용 프로그램만 빌드합니다.

32비트 x86 빌드는 디컴프의 호스트 코드와 x86 블록 번역기를 그대로 쓰는 유일한 환경입니다(docs/64bit.ko.md).

```sh
sudo dpkg --add-architecture i386 && sudo apt-get update
sudo apt-get install gcc-multilib g++-multilib libsdl2-dev:i386
cmake -S port -B build-i686 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/linux-i686.cmake
```

## Android

JDK 17, Android SDK(platform 34), NDK `27.2.12479018`, CMake `3.22.1`
(`sdkmanager "platforms;android-34" "ndk;27.2.12479018" "cmake;3.22.1"`), 그리고 SDL2 소스가 필요합니다.

```sh
git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git android/SDL
cd android
./gradlew assembleRelease      # app/build/outputs/apk/release/app-release.apk
```

APK에는 `armeabi-v7a`와 `arm64-v8a`가 들어갑니다. `-PsdlDir=/SDL/경로`로 다른 곳의 SDL2를,
`-PdecompRoot=/acww-decomp/경로`로 다른 디컴프 체크아웃을 쓸 수 있고, `-Pabis=x86_64`는 에뮬레이터
이미지용으로 빌드합니다. release APK는 당분간 debug 키로 서명됩니다.

휴대폰에서 실행을 시작하지 못하면 앱이 로그의 끝부분을 보여 줍니다. 전체 로그는 앱 파일의
`log.txt`입니다(`log-1.txt`는 그 전 실행).

## 테스트

```sh
python3 port/tests/test_rom_prep.py build/acww-rom-prep
```

합성 DS ROM(압축된 ARM9, autoload, 압축·비압축 오버레이)을 만들어 포트의 C 코드와 디컴프의
`tools/extract.py`로 각각 나눈 뒤 모듈을 바이트 단위로 비교합니다. 거부해야 하는 경우(다른 지역판,
ROM이 아닌 파일, 없는 파일)도 확인합니다. CI가 푸시할 때마다 이 테스트와 두 플랫폼 빌드를 돌립니다
(`.github/workflows/build.yml`).

```sh
python3 port/tests/test_frames.py 경로/한국판.nds build
```

디컴프의 녹화를 빌드에서 재생해 프레임을 디컴프의 것과 비트 단위로 비교합니다. ROM이 필요하므로 직접
돌려야 합니다. 통과한 환경과 qemu-user로 ARM용을 돌리는 방법은 docs/64bit.ko.md에 있습니다.

## 구성

| 경로 | 내용 |
|---|---|
| `decomp/` | [acww-decomp](https://github.com/banatic/acww-decomp) 서브모듈. 고치지 않고 그대로 빌드 |
| `port/CMakeLists.txt` | 디컴프 런타임(Reference 형태)과 화면 코드를 Linux·Android용으로 빌드 |
| `port/src/front_sdl.c` | SDL2 화면 코드(디컴프의 `runtime/play.c`를 SDL2로 옮김) |
| `port/src/touch.c` | 터치 버튼과 화면 배치 |
| `port/src/tall3d.c`, `gpu3d_main.c`, `gpu3d_tile.c` | 이어진 화면과 더 큰 그림: 디컴프 3D 엔진의 복제본(docs/tall-view.ko.md, docs/renderer.ko.md) |
| `port/src/mix2d.c`, `gpu2d_main.c`, `gpu2d_probe.c` | 큰 3D 그림을 2D 레이어와 섞기: 디컴프 2D 엔진의 복제본 |
| `port/src/sound_sdl.c` | 소리: 디컴프의 `sound.c`에 SDL 오디오 장치를 붙임 |
| `port/src/game_hooks.c`, `run_port.c` | 게임 코드에 거는 훅: 화면 위쪽 그리기, 도루묵씨 |
| `port/src/rom_prep.c` | ROM 확인과 모듈 분리 |
| `port/src/native_os_portable.c` | 디컴프의 x86 전용 호스트 코드를 대신하는 코드 |
| `port/compat/` | 디컴프 소스에 강제로 포함하는 헤더. Android C 라이브러리, 64비트 환경 대응 |
| `port/tests/` | ROM 분리를 디컴프와 비교(ROM 불필요), 프레임을 디컴프와 비교(본인 ROM 필요) |
| `android/` | Gradle 프로젝트: `LaunchActivity`(ROM 요청), `MainActivity`(SDL), `SettingsActivity`(설정) |
