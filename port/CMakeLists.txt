# The port: acww-decomp's runtime (the Reference form, docs/roadmap.md) behind an SDL2 front end.
#
#   Linux:   cmake -S port -B build        (add -DCMAKE_TOOLCHAIN_FILE=port/cmake/linux-i686.cmake for 32-bit x86)
#   Android: android/app/build.gradle builds this file for each ABI (docs/building.md)
#
# The decomp's runtime maps the DS's memory at the DS's own addresses and uses DS addresses as
# host pointers (decomp/runtime/memory.c). It is written as a 32-bit program; the Reference
# form, which is what the port runs, also works in a 64-bit process, where those addresses are
# as free (docs/64bit.md). 32-bit x86 Linux is the one host that builds the decomp's own host
# code, with its x86 block translator.
cmake_minimum_required(VERSION 3.16)
project(acww_kor_port C)

set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../decomp" CACHE PATH "the acww-decomp checkout (the decomp/ submodule)")
set(SDL_SOURCE_DIR "" CACHE PATH "an SDL2 source tree to build with (Android); else the system's SDL2 is used")
option(PORT_FRONT_END "build the SDL2 front end (OFF: only the headless test programs, which need no SDL2)" ON)
set(RUNTIME "${DECOMP_ROOT}/runtime")

if(NOT EXISTS "${RUNTIME}/run.c")
    message(FATAL_ERROR "acww-decomp not found at ${DECOMP_ROOT}: run `git submodule update --init` or set DECOMP_ROOT")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PORT_VERSION)
string(STRIP "${PORT_VERSION}" PORT_VERSION)

# The decomp's own host code (x86 calls, fault frames and fibers) where it was written for it:
# 32-bit x86 Linux and Windows. Elsewhere, Android included, the port's stand-ins.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$" AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT ANDROID)
    set(DECOMP_HOST ON)
    enable_language(ASM)
else()
    set(DECOMP_HOST OFF)
endif()

# decomp/tools/run.py's SOURCES, the Reference form (native_none.c: no Native functions);
# run.c is built with each program, with or without its own main (RUN_FRONT_END)
set(RUNTIME_SOURCES
    arm.c memory.c io.c bios.c arm7.c boot.c gpu2d.c frame.c input.c native_run.c
    native_aggregate.c render.c drive.c sequencer.c ds_audio.c sound.c wm.c wifi.c driven.c native_none.c)
if(DECOMP_HOST)
    list(APPEND RUNTIME_SOURCES native_os.c host.c native_tramp.S)
endif()
list(TRANSFORM RUNTIME_SOURCES PREPEND "${RUNTIME}/")

# gpu3d.c is built twice, through two files that include it: the engine itself, and a second
# copy for the tall view, with tall3d.c in front of both (port/src/tall3d.h)
add_library(acww_runtime STATIC ${RUNTIME_SOURCES} src/gpu3d_main.c src/gpu3d_upper.c src/tall3d.c)
target_include_directories(acww_runtime PUBLIC src)
if(NOT DECOMP_HOST)
    target_sources(acww_runtime PRIVATE src/native_os_portable.c)
endif()
target_include_directories(acww_runtime PUBLIC "${RUNTIME}")
# The decomp's flags (decomp/tools/run.py, build()); its sources are built as they are, with
# the port's compatibility header in front of each (port/compat/port_compat.h).
target_compile_options(acww_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=c11 -O2 -fwrapv -fno-strict-aliasing -w>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/compat/port_compat.h>)
target_compile_definitions(acww_runtime PRIVATE $<$<COMPILE_LANGUAGE:C>:_DEFAULT_SOURCE>)
set_source_files_properties(src/native_os_portable.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    # memory.c refuses to build for anything but 32 bits; see the header for why it may
    set_source_files_properties("${RUNTIME}/memory.c" PROPERTIES COMPILE_OPTIONS
        "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/memory_64bit.h")
endif()
set(RUN_C_OPTIONS -std=c11 -O2 -fwrapv -fno-strict-aliasing -w "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/port_compat.h")
find_package(Threads REQUIRED)
target_link_libraries(acww_runtime PUBLIC Threads::Threads m)

# The ROM check and split, which the tests also run without a window.
add_library(acww_rom STATIC src/rom_prep.c src/sha1.c)
target_include_directories(acww_rom PUBLIC src)
target_compile_options(acww_rom PRIVATE -std=c11 -Wall -Wextra)
target_compile_definitions(acww_rom PRIVATE _DEFAULT_SOURCE)

# The decomp's headless program (run.c's own main) and the ROM split on their own, for the
# tests: port/tests/test_frames.py plays the decomp's recordings and compares the frames.
if(NOT ANDROID)
    add_executable(acww-run tools/run_headless.c)
    target_include_directories(acww-run PRIVATE "${RUNTIME}")
    target_compile_options(acww-run PRIVATE ${RUN_C_OPTIONS})
    target_compile_definitions(acww-run PRIVATE _DEFAULT_SOURCE)
    target_link_libraries(acww-run PRIVATE acww_runtime)

    add_executable(acww-rom-prep tools/rom_prep_main.c)
    target_link_libraries(acww-rom-prep PRIVATE acww_rom)
    target_compile_options(acww-rom-prep PRIVATE -std=c11 -Wall -Wextra)
endif()
if(DECOMP_HOST)
    # as the decomp links its programs: at a fixed address, which costs the interpreter
    # nothing for position independence
    set_target_properties(acww_runtime PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    target_compile_options(acww_runtime PRIVATE -fno-pie)
    target_link_options(acww-run PRIVATE -no-pie -Wl,-z,noexecstack)
endif()
if(NOT PORT_FRONT_END)
    return()
endif()

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

set(FRONT_SOURCES src/front_sdl.c src/touch.c "${RUNTIME}/run.c")
set_source_files_properties("${RUNTIME}/run.c" PROPERTIES COMPILE_OPTIONS "${RUN_C_OPTIONS}")
if(ANDROID)
    # SDLActivity loads libSDL2.so and libmain.so, and calls SDL_main
    add_library(main SHARED ${FRONT_SOURCES})
    set(FRONT main)
else()
    add_executable(acww-kor ${FRONT_SOURCES})
    set(FRONT acww-kor)
    if(DECOMP_HOST)
        set_target_properties(acww-kor PROPERTIES POSITION_INDEPENDENT_CODE OFF)
        target_link_options(acww-kor PRIVATE -no-pie -Wl,-z,noexecstack)
    endif()
endif()
set_source_files_properties(${FRONT_SOURCES} PROPERTIES COMPILE_DEFINITIONS RUN_FRONT_END)
set_source_files_properties(src/front_sdl.c src/touch.c PROPERTIES COMPILE_OPTIONS "-std=c11;-Wall;-Wextra")
target_compile_definitions(${FRONT} PRIVATE _DEFAULT_SOURCE PORT_VERSION="${PORT_VERSION}")
target_link_libraries(${FRONT} PRIVATE acww_rom acww_runtime ${SDL_TARGET} m)
