<p align="center"><a href="64bit.md">English</a> · <b>한국어</b></p>

# 64비트 환경과 ARM

디컴프 런타임은 32비트 x86 프로그램으로 작성되어 있습니다. 포트는 이것을 64비트 환경과 ARM용으로도
빌드합니다. 휴대폰이 바로 그런 환경이기 때문입니다. 이 문서는 그렇게 해도 되는 이유, 검증 방법,
빠진 부분을 설명합니다.

## 왜 되는가

런타임은 DS 메모리를 DS의 원래 주소(메인 RAM `0x02000000`, ITCM `0x01ff8000`, DTCM `0x027e0000`)에
매핑하고 DS 주소를 그대로 포인터로 씁니다. 그래서 `runtime/memory.c`는 포인터가 32비트가 아니면 빌드를
거부하고, 그 낮은 주소들이 비어 있어야 합니다.

휴대폰에서는 둘 다 성립하지 않습니다. 포인터는 64비트이고, 낮은 주소는 비어 있지 않습니다. Android 앱은
Java 런타임의 메모리가 이미 매핑된 채로 시작하는데, ARM에서는 DS 메모리가 들어갈 바로 그 범위가
포함됩니다(0.5.0은 실제 휴대폰에서 "cannot map the DS memory"로 멈췄습니다. Java 런타임이 다른 곳에
할당하는 x86-64 에뮬레이터에서는 드러나지 않았습니다).

그래서 디컴프의 호스트 코드와 블록 번역기를 그대로 쓰는 32비트 x86 Linux를 뺀 모든 환경에서, 포트는 DS
메모리를 **시스템이 내주는 위치**에 두고 기준 주소를 더합니다.

- `port/src/memory_based.c`가 DS 주소 범위를 모두 담을 만큼의 주소 공간을 한 덩어리로 잡고, 그 안의 해당
  오프셋에 각 영역을 매핑합니다. DS 주소에 `memory_base`를 더하면 호스트 포인터가 됩니다.
- 런타임은 `memory.h`의 `memory_host`를 거쳐 DS 메모리에 접근하고, `arm.c`의 다섯 군데에서만 주소를 직접
  포인터로 씁니다. 빌드가 디컴프의 파일로부터 기준 주소를 더한 `memory.h`와 `arm.c`를 만들어 씁니다
  (`port/CMakeLists.txt`). 바꿔야 할 문장을 찾지 못하면 빌드가 멈추므로, 디컴프가 바뀌어도 모르고 지나가지
  않습니다.
- **Reference 형태**(모든 함수를 ROM의 코드 그대로 인터프리터에서 실행. 포트가 쓰는 형태)에서는 그 밖에 DS
  주소를 포인터로 쓰는 곳이 없습니다. 64비트로 경고 없이 컴파일되고, 아래의 프레임 검증이 그 증거입니다.
  컴파일된 코드가 구조체에 DS 포인터를 담는 Native·Clean 형태는 빌드하지 않습니다.
- 디컴프의 x86 전용 호스트 코드는 대체 코드로 바꿉니다(`port/src/native_os_portable.c`).

## 어떻게 검증하는가

디컴프에는 입력 녹화와, 각 녹화가 만들어 내야 하는 프레임의 SHA-1이 들어 있고
(`decomp/runtime/recordings/`), 디컴프는 32비트 x86 PC에서 이것으로 스스로를 검사합니다.
`port/tests/test_frames.py`는 같은 녹화를 포트 빌드에서 재생해 비교합니다.

```sh
python3 port/tests/test_frames.py 한국판.nds build                # 이 PC
python3 port/tests/test_frames.py 한국판.nds build-aarch64 --run "qemu-aarch64 -L /usr/aarch64-linux-gnu"
```

| 환경 | `title-start` (1,300프레임) | `new-game` (13,760프레임) |
|---|---|---|
| x86 32비트 (디컴프의 호스트 코드와 블록 번역기 그대로) | 프레임 일치 | 프레임 일치 |
| x86-64 | 프레임 일치 | 프레임 일치 |
| ARM 64비트 (`aarch64`, qemu-user) | 프레임 일치 | 프레임 일치 |
| ARM 32비트 (`armhf`, qemu-user) | 프레임 일치 | 프레임 일치 |

"프레임 일치"는 모든 프레임의 두 화면이 비트 단위로 같다는 뜻입니다. ARM 빌드는 크로스 컴파일러로
만든 Linux 프로그램이고(`port/cmake/linux-aarch64.cmake`, `linux-armhf.cmake`,
`-DPORT_FRONT_END=OFF`), Android 라이브러리는 같은 소스를 NDK로 빌드한 것입니다.

## 휴대폰에서 다른 점

- **속도.** ARM용 블록 번역기가 없어서(디컴프의 것은 x86 코드) 모든 명령을 해석하고, 메모리 접근마다 기준
  주소를 더합니다.

## 빠진 것

Native와 Clean 형태. 32비트 포인터가 필요하고, 지금 코드로는 x86도 필요합니다(docs/roadmap.ko.md).
