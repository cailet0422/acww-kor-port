<p align="center"><a href="roadmap.md">English</a> · <b>한국어</b></p>

# 로드맵

포트가 지금 무엇을 하는지, 어느 휴대폰에서나 잘 돌아가는 게임이 되기까지 무엇이 남았는지, 어떤 순서로
진행할지를 정리합니다. 디컴프에 관한 내용은 고정해 둔 커밋의 `decomp/`(그 안의 `docs/runtime.md`,
`docs/play.md`, 런타임 소스)를 기준으로 합니다.

## 현재 상태 (0.1.0)

| | 상태 |
|---|---|
| Linux, 32비트 x86 | 빌드·링크됨(CI). ROM이 없거나 틀리면 이유를 알려 주고 종료 |
| Android, `armeabi-v7a` | APK 빌드됨(CI). **아직 기기에서 실행해 보지 않음** |
| ROM 확인과 분리 | 합성 ROM에서 디컴프의 `tools/extract.py`와 바이트 단위로 같은 모듈을 만듦(`port/tests/test_rom_prep.py`). SHA-1도 Python 결과와 대조 |
| 게임 자체 | 포트에서는 미검증. 빌드 환경에 ROM도 휴대폰도 없음 |
| 소리 | 없음(아래 참고) |
| 64비트 전용 휴대폰 | 설치 불가(아래 참고) |

## 디컴프 런타임의 전제

1. **32비트 프로세스.** `runtime/memory.c`는 다른 환경에서 빌드를 멈춥니다(`#error "the runtime is
   a 32-bit program: DS addresses are host pointers"`). DS의 메인 RAM, ITCM, DTCM을 DS의 원래
   주소(`0x02000000` 등)에 매핑하고, 인터프리터가 DS 주소를 그대로 포인터로 씁니다. 그래서 포트는
   `armeabi-v7a`(에뮬레이터용으로는 `x86`)만 빌드합니다. 2023년 이후 나온 휴대폰 중 상당수는 64비트
   앱만 실행하므로 설치할 수 없습니다.
2. **컴파일된 형태는 x86 전용.** Native와 Clean 형태는 x86 호출 규약으로 컴파일된 함수를 부르고
   (`runtime/host.c`는 다른 환경에서 빌드를 멈춤), x86 예외 프레임으로 DS 코드 호출을 잡아내고
   (`runtime/native_os.c`의 `REG_EIP`), 코드에 x86 점프를 덧쓰고, x86 어셈블리로 인터프리터에
   들어갑니다(`runtime/native_tramp.S`). 포트는 이 파일들을 Reference 형태만 돌아가게 하는 대체
   코드로 바꿉니다.
3. **인터프리터 가속도 x86 코드.** 자주 실행되는 블록을 x86 코드로 번역합니다(`runtime/arm_x86.c`,
   `__i386__`에서만). ARM에서는 모든 명령을 해석합니다.
4. **창과 소리는 Win32.** `runtime/play.c`(창)와 `runtime/sound.c`의 출력 장치 부분(`waveOut`)은
   Windows 전용입니다. 믹서 자체는 아닙니다.
5. **glibc 세부 사항.** `memfd_create`(Android C 라이브러리에는 API 30부터)와 `_DEFAULT_SOURCE`에서의
   `fseeko`/`ftello`. 둘 다 `port/compat/port_compat.h`가 처리합니다.

## 다음 할 일

1. **휴대폰에서 실행해 보기.** 32비트 Android 앱에서 DS 주소를 매핑할 수 있는지 확인합니다(보통
   Android가 쓰지 않는 낮은 주소지만 보장은 없습니다). ARM에서 인터프리터 속도를 재고, 깨지는 곳을
   고칩니다. 그다음 터치 버튼을 다듬고, 앱에서 세이브를 가져오기·내보내기와 ROM 바꾸기를 할 수 있게
   합니다.
2. **소리.** `waveOut` 부분 대신 디컴프의 믹서(`runtime/sound.h`, `runtime/ds_audio.h`)에 SDL 오디오
   장치를 붙입니다.
3. **속도(휴대폰에서 인터프리터가 너무 느리면).** 손이 덜 가는 순서로, 컴파일러 옵션과 프로파일링,
   블록 번역기의 ARM 버전(`arm_x86.c`가 구조를 보여 줌), ARM에서의 Native·Clean 형태입니다. 마지막은
   ARM용 `host_invoke`(AAPCS 호출), ARM 예외 프레임, `makecontext` 없는 파이버(Android C
   라이브러리에는 없음)가 필요합니다.
4. **64비트 휴대폰.** 가장 큰 일입니다. Reference 형태는 인터프리터의 메모리 접근이 DS 주소를 호스트
   포인터로 쓰지 않고 기준 주소를 거치게 바꿔야 합니다. 컴파일된 형태는 더 어렵습니다. 디컴프의
   구조체가 32비트 DS 포인터를 담고 있기 때문입니다. 디컴프 문서도 포팅 작업은 별도 프로젝트에서
   하라고 하니, 디컴프 관리자와 협의가 필요합니다.
5. **업스트림 기여.** 디컴프 런타임이 x86이 아닌 환경에서도 그대로 빌드되도록 작은 변경
   (`host.c`, `native_tramp.S`, x86 예외 처리 코드를 조건부로 감싸기)을 제안해 포트의 대체 코드를 줄입니다.
