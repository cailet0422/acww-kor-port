<p align="center"><a href="README.md">English</a> · <b>한국어</b></p>

<h1 align="center">acww-kor-port</h1>

<p align="center">
  <i>놀러오세요 동물의 숲</i>(Animal Crossing: Wild World, 닌텐도 DS) 한국판(<code>ADMK</code>)의 Android(및 Linux) 포트입니다.<br>
  <a href="https://github.com/banatic/acww-decomp">acww-decomp</a> 디컴파일을 기반으로 합니다.
</p>

<p align="center">
  <a href="https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml"><img alt="build" src="https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml/badge.svg"></a>
</p>

> [!IMPORTANT]
> **게임 데이터는 포함되어 있지 않습니다.** 직접 덤프한 한국판 카트리지 ROM(`ADMK`, SHA-1
> `1fffd601d3439e37cd934e72d08e487a93fc449b`)이 필요합니다. 사용 영역만 남긴(trimmed) 덤프도 받습니다.
> 게임 코드는 기기에서 이 ROM으로부터 읽어 오며, 이 저장소나 빌드에는 ROM의 내용이 전혀 들어 있지 않습니다.

> [!WARNING]
> **초기 단계입니다(0.10.0).** 포트에서 게임이 실행되고(휴대폰 포함), x86과 ARM, 32비트와 64비트 모두에서
> 프레임이 디컴프의 것과 비트 단위로 같습니다([docs/64bit.ko.md](docs/64bit.ko.md)). 소리는
> [docs/sound.ko.md](docs/sound.ko.md), 앞으로의 계획은 [로드맵](docs/roadmap.ko.md)을 참고하세요.

## 무엇인가

[acww-decomp](https://github.com/banatic/acww-decomp)는 게임의 ARM9 코드를 디컴파일하고, 그것을
PC에서 실행하는 런타임을 갖고 있습니다. 모든 함수는 인터프리터에서 ROM의 원래 코드로 실행되거나
(*Reference* 형태), 일치(matching) 소스를 PC용으로 컴파일해 실행되거나, 읽기 쉬운 재작성 버전으로
실행될 수 있습니다. 다만 화면 창은 Windows 전용이고, 컴파일된 형태는 32비트 x86 PC가 필요합니다.

이 저장소는 디컴프를 고치지 않고(`decomp/` 서브모듈) 다른 플랫폼에 필요한 부분을 더합니다.

- **SDL2 화면 코드**(`port/src/front_sdl.c`): 디컴프의 Win32 창을 대신합니다. 두 화면, 키보드,
  마우스, 게임패드, 화면 둘레에 그리는 터치 버튼, 프레임 속도 맞춤, 세이브 파일을 맡습니다.
- **이어진 화면**: 3D 장면을 위 화면까지 이어 그려서 두 화면이 한 장의 그림이 됩니다
  ([docs/tall-view.ko.md](docs/tall-view.ko.md)).
- **Vulkan 렌더러**(실험적, 기본은 끔): 3D 장면을 GPU가 화면 해상도 그대로, 안티에일리어싱과 함께 그리고
  게임의 2D 레이어를 섞습니다. 또는 소프트웨어로 장면을 2배·3배 크기로 그립니다(무거움)
  ([docs/renderer.ko.md](docs/renderer.ko.md)).
- **같이 하기**(실험적): DS의 로컬 무선 통신을 포트끼리 네트워크로 잇습니다. 기기 하나가 방을 열거나 중계
  서버를 씁니다. 닌텐도 Wi-Fi 커넥션은 기기의 네트워크로 나가며, 지정한 DNS가 가리키는 서버에 접속합니다
  ([docs/multiplayer.ko.md](docs/multiplayer.ko.md)).
- **소리**: 런타임의 믹서 출력을 SDL 오디오 장치로 냅니다(`port/src/sound_sdl.c`. 디컴프의 출력 장치는
  Windows 전용).
- **ROM 확인과 분리**를 기기에서 합니다(`port/src/rom_prep.c`). 디컴프의 `tools/extract.py`와
  같은 방식으로 나누며, 그 결과와 같은지 테스트합니다.
- 디컴프의 x86 전용 호스트 코드를 대신하는 **작은 대체 코드**를 둬서 런타임이 ARM과 64비트 환경용으로
  빌드되게 합니다(`port/src/native_os_portable.c`, `port/compat/`, [docs/64bit.ko.md](docs/64bit.ko.md)).
- **Android 앱**(`android/`): 처음 한 번 ROM을 고르게 한 뒤 게임을 시작합니다.
- 푸시할 때마다 둘 다 빌드하고 테스트를 돌리는 **CI**.

포트는 Reference 형태로 실행합니다. DS에서처럼 게임 코드를 인터프리터로 해석해 돌립니다.

## 빌드 받기

푸시할 때마다 APK와 Linux 실행 파일이 만들어집니다.
[Actions](https://github.com/cailet0422/acww-kor-port/actions/workflows/build.yml)에서 최근 실행을
열고 artifacts를 내려받으세요. 아직 정식 릴리스는 없습니다.

| 플랫폼 | 필요한 것 |
|---|---|
| Android | Android 5.0 이상, ARM 32비트 또는 64비트, OpenGL ES 2.0 |
| Linux | x86-64와 SDL2(`libsdl2-2.0-0`). 32비트 x86 빌드도 함께 만들어짐 |

Android에서는 처음 실행할 때 ROM 파일을 고르면 앱 저장공간으로 복사합니다. Linux에서는
`acww-kor --rom 경로/한국판.nds`로 실행합니다. 직접 빌드하는 방법은 [docs/building.ko.md](docs/building.ko.md)에 있습니다.

## 조작

| DS | 키보드 | 게임패드 | 터치 화면 |
|---|---|---|---|
| 십자 버튼 | 방향키 | 십자키, 왼쪽 스틱 | 왼쪽 패드(세로 화면에서는 아래) |
| A, B, X, Y | X, Z, S, A | 위치가 같은 버튼 | 오른쪽 버튼 |
| L, R | Q, W | 숄더, 트리거 | 위쪽 모서리 |
| START, SELECT | Enter, Shift 또는 Backspace | Start, Back | 아래쪽 |
| 터치스크린 | 아래 화면에서 마우스 | | 아래 화면을 손가락으로 |

Android에서는 점 세 개 표시를 누르면 설정이 열립니다(한국어가 기본이고, 시스템 언어가 영어이면 영어).
소리, 자동저장, 도루묵씨, 풀터치 모드(버튼 없이 터치펜 조작만), 이어진 화면, 버튼 배치 편집, 세이브 내보내기와
가져오기, 다른 ROM 선택이 있습니다. 버튼 배치 편집에서는 각 버튼을 원하는 곳(그림 위 포함)으로 끌어
옮기고, 마지막으로 만진 버튼을 −와 +로 줄이거나 키우고, RESET으로 전부 원래대로 돌리고, 체크 표시로
닫습니다. 세로 화면과 가로 화면의 배치는 따로 기억됩니다. 뒤로 버튼을 누르면 게임을 종료할지 묻습니다.
(Android가 아닌 터치 화면에서는 표시를 짧게 누르면 버튼 켜기/끄기, 길게 누르면 배치 편집입니다.) Tab을 누르고 있으면 기기가 낼 수 있는 최대 속도로 돌고, F12는 두 화면을 세이브 옆에 PNG로 저장합니다.

포트는 게임이 저장을 허용할 순간에 1분마다, 그리고 앱을 나갈 때 마을을 세이브 옆의 파일에 따로 저장하고,
다음 실행 때 거기서부터 이어서 할지 묻습니다([docs/autosave.ko.md](docs/autosave.ko.md), `autosave=0`이나
`--no-autosave`로 끔). 저장하지 않고 끝내도 도루묵씨는 나오지 않습니다(`settings.txt`의 `resetti=1`이나
`--resetti`로 DS처럼 다시 나오게 할 수 있습니다).

세이브는 256KB 플래시 이미지(에뮬레이터가 쓰는 `.sav` 형식)입니다. Android에서는 앱 파일 안의
`acww.sav`, Linux에서는 `~/.local/share/acww-kor-port/acww-kor/`에 있으며 `--data`와 세이브 경로로
바꿀 수 있습니다. 게임이 세이브 칩에 쓰기를 멈추면 임시 파일을 거쳐 기록하고, 기존 파일을 처음
덮어쓸 때는 원래 파일을 `.bak`으로 남깁니다.

## 라이선스

포트의 코드(`port/`, `android/`, CI)는 [MIT 라이선스](LICENSE)입니다. `decomp/`의 acww-decomp는
[CC0 1.0](https://github.com/banatic/acww-decomp/blob/main/LICENSE)이며, 화면 코드는 그 `runtime/play.c`를
따릅니다. 빌드할 때 받는 SDL2는 zlib 라이선스입니다. *동물의 숲*은 닌텐도의 것이며, 이 프로젝트는
닌텐도와 관계가 없습니다.
