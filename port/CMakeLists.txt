# The port: acww-decomp's runtime (the Reference form, docs/roadmap.md) behind an SDL2 front end.
#
#   Desktop (32-bit x86 Linux):  cmake -S port -B build -DCMAKE_TOOLCHAIN_FILE=port/cmake/linux-i686.cmake
#   Android: android/app/build.gradle builds this file for each ABI (docs/building.md)
#
# The decomp's runtime is a 32-bit program: DS addresses are host pointers, and the DS's memory
# is mapped at the DS's own addresses (decomp/runtime/memory.c). So every build here is 32-bit.
cmake_minimum_required(VERSION 3.16)
project(acww_kor_port C)

set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../decomp" CACHE PATH "the acww-decomp checkout (the decomp/ submodule)")
set(SDL_SOURCE_DIR "" CACHE PATH "an SDL2 source tree to build with (Android); else the system's SDL2 is used")
set(RUNTIME "${DECOMP_ROOT}/runtime")

if(NOT EXISTS "${RUNTIME}/run.c")
    message(FATAL_ERROR "acww-decomp not found at ${DECOMP_ROOT}: run `git submodule update --init` or set DECOMP_ROOT")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "the decomp's runtime is a 32-bit program (DS addresses are host pointers): build for a 32-bit "
                        "target (armeabi-v7a or x86 on Android, port/cmake/linux-i686.cmake on Linux). See docs/roadmap.md.")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PORT_VERSION)
string(STRIP "${PORT_VERSION}" PORT_VERSION)

# The decomp's own host code (x86 calls, fault frames and fibers) where it was written for it:
# 32-bit x86 Linux and Windows. Elsewhere, Android included, the port's stand-ins.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$" AND NOT ANDROID)
    set(DECOMP_HOST ON)
    enable_language(ASM)
else()
    set(DECOMP_HOST OFF)
endif()

# decomp/tools/run.py's SOURCES, the Reference form (native_none.c: no Native functions)
set(RUNTIME_SOURCES
    arm.c memory.c io.c bios.c arm7.c boot.c gpu2d.c gpu3d.c frame.c input.c run.c native_run.c
    native_aggregate.c render.c drive.c sequencer.c ds_audio.c sound.c wm.c wifi.c driven.c native_none.c)
if(DECOMP_HOST)
    list(APPEND RUNTIME_SOURCES native_os.c host.c native_tramp.S)
endif()
list(TRANSFORM RUNTIME_SOURCES PREPEND "${RUNTIME}/")

add_library(acww_runtime STATIC ${RUNTIME_SOURCES})
if(NOT DECOMP_HOST)
    target_sources(acww_runtime PRIVATE src/native_os_portable.c)
endif()
target_include_directories(acww_runtime PUBLIC "${RUNTIME}")
# The decomp's flags (decomp/tools/run.py, build()); its sources are built as they are, with
# the port's compatibility header in front of each (port/compat/port_compat.h).
target_compile_options(acww_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=c11 -O2 -fwrapv -fno-strict-aliasing -w
    -include "${CMAKE_CURRENT_SOURCE_DIR}/compat/port_compat.h">)
target_compile_definitions(acww_runtime PRIVATE $<$<COMPILE_LANGUAGE:C>:_DEFAULT_SOURCE> RUN_FRONT_END)
set_source_files_properties(src/native_os_portable.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
find_package(Threads REQUIRED)
target_link_libraries(acww_runtime PUBLIC Threads::Threads m)

# The ROM check and split, which the tests also run without a window.
add_library(acww_rom STATIC src/rom_prep.c src/sha1.c)
target_include_directories(acww_rom PUBLIC src)
target_compile_options(acww_rom PRIVATE -std=c11 -Wall -Wextra)
target_compile_definitions(acww_rom PRIVATE _DEFAULT_SOURCE)

# SDL2
if(SDL_SOURCE_DIR)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${SDL_SOURCE_DIR}" SDL2 EXCLUDE_FROM_ALL)
    set(SDL_TARGET SDL2::SDL2)
    if(NOT TARGET SDL2::SDL2)
        set(SDL_TARGET SDL2)
    endif()
else()
    find_package(SDL2 REQUIRED CONFIG)
    set(SDL_TARGET SDL2::SDL2)
endif()

set(FRONT_SOURCES src/front_sdl.c src/touch.c)
if(ANDROID)
    # SDLActivity loads libSDL2.so and libmain.so, and calls SDL_main
    add_library(main SHARED ${FRONT_SOURCES})
    set(FRONT main)
else()
    add_executable(acww-kor ${FRONT_SOURCES})
    set(FRONT acww-kor)
    if(DECOMP_HOST)
        # as the decomp links its programs: at a fixed address, which costs the interpreter
        # nothing for position independence
        set_target_properties(acww_runtime acww-kor PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_compile_options(acww_runtime PRIVATE -fno-pie)
        target_link_options(acww-kor PRIVATE -no-pie -Wl,-z,noexecstack)
    endif()

    add_executable(acww-rom-prep tools/rom_prep_main.c)
    target_link_libraries(acww-rom-prep PRIVATE acww_rom)
    target_compile_options(acww-rom-prep PRIVATE -std=c11 -Wall -Wextra)
endif()
target_compile_options(${FRONT} PRIVATE -std=c11 -Wall -Wextra)
target_compile_definitions(${FRONT} PRIVATE _DEFAULT_SOURCE PORT_VERSION="${PORT_VERSION}")
target_link_libraries(${FRONT} PRIVATE acww_rom acww_runtime ${SDL_TARGET} m)
