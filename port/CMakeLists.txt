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

# Where the DS's memory is. The decomp maps it at the DS's own addresses, so that a DS address
# is a host pointer. A process cannot count on those low addresses (on ARM phones the Java
# runtime has its own memory there), so everywhere but on the decomp's own host the port maps
# it where the system has room and adds a base to every address (src/memory_based.c). The
# decomp's sources reach memory through memory_host of memory.h, and arm.c in five places by
# using an address as a pointer: both are made here from the decomp's files, with the base
# added. A text that is no longer there stops the build: the decomp has changed, and this
# has to be looked at again.
function(port_replace var old new)
    string(FIND "${${var}}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "the decomp's source has changed: '${old}' is not in it any more "
                            "(port/CMakeLists.txt, the based memory)")
    endif()
    string(REPLACE "${old}" "${new}" out "${${var}}")
    set(${var} "${out}" PARENT_SCOPE)
endfunction()
function(port_write path text)
    set(old "")
    if(EXISTS "${path}")
        file(READ "${path}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${path}" "${text}")
    endif()
endfunction()
set(RUNTIME_INCLUDES "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/port_compat.h")
if(NOT DECOMP_HOST)
    set(BASED "${CMAKE_CURRENT_BINARY_DIR}/based")
    file(READ "${RUNTIME}/memory.h" text)
    port_replace(text "#include <stdint.h>\n" "#include <stdint.h>\n\n/* (the port) where the DS's address 0 is in this process: src/memory_based.c */\nextern uint8_t *memory_base;\n")
    port_replace(text "        return (uint8_t *) (uintptr_t) address;" "        return memory_base + address;")
    port_write("${BASED}/memory.h" "${text}")
    file(READ "${RUNTIME}/arm.c" text)
    port_replace(text "memcpy(&h, (const void *) (uintptr_t) address, 2);" "memcpy(&h, (const void *) (memory_base + address), 2);")
    port_replace(text "memcpy(&instr, (const void *) (uintptr_t) address, 4);" "memcpy(&instr, (const void *) (memory_base + address), 4);")
    port_replace(text "memcpy(b->ops + count, (const void *) (uintptr_t) pc, bytes);" "memcpy(b->ops + count, (const void *) (memory_base + pc), bytes);")
    port_replace(text "const uint8_t *now = (const uint8_t *) (uintptr_t) b->start;" "const uint8_t *now = (const uint8_t *) (memory_base + b->start);")
    port_replace(text "memcpy((void *) (uintptr_t) check_log[0][i].address," "memcpy((void *) (memory_base + check_log[0][i].address),")
    string(FIND "${text}" "(uintptr_t)" left)
    if(NOT left EQUAL -1)
        message(FATAL_ERROR "the decomp's arm.c uses an address as a pointer in a place the port does not know "
                            "(port/CMakeLists.txt, the based memory)")
    endif()
    port_write("${BASED}/arm.c" "${text}")
    # the made memory.h in front of every source of the runtime: the decomp's own, found
    # beside the sources, is then skipped by its include guard
    list(APPEND RUNTIME_INCLUDES "-include${BASED}/memory.h")
endif()

# decomp/tools/run.py's SOURCES, the Reference form (native_none.c: no Native functions);
# run.c is built with each program, with or without its own main (RUN_FRONT_END), and so are
# sound.c and ds_audio.c: as they are for the headless program; for the front end with an SDL
# device (src/sound_sdl.c) and with the DS's data fetching and output rate (src/ds_audio_port.c)
set(RUNTIME_SOURCES
    arm.c memory.c io.c bios.c arm7.c boot.c frame.c input.c native_run.c
    native_aggregate.c render.c drive.c sequencer.c wm.c wifi.c driven.c native_none.c)
if(DECOMP_HOST)
    list(APPEND RUNTIME_SOURCES native_os.c host.c native_tramp.S)
else()
    list(REMOVE_ITEM RUNTIME_SOURCES arm.c memory.c)
endif()
list(TRANSFORM RUNTIME_SOURCES PREPEND "${RUNTIME}/")
if(NOT DECOMP_HOST)
    list(APPEND RUNTIME_SOURCES "${BASED}/arm.c" src/memory_based.c)
endif()

# gpu3d.c and gpu2d.c are built several times, through files of the port that include them:
# the engines themselves, copies of the 3D engine for the parts of the picture beyond the
# game's own (the tall view, a larger size: src/tall3d.h), and copies of the 2D engines for
# mixing that picture with the game's 2D layers (src/mix2d.h)
add_library(acww_runtime STATIC ${RUNTIME_SOURCES} src/gpu3d_main.c src/tall3d_engines.c src/tall3d.c
    src/gpu2d_main.c src/mix2d.c)
target_include_directories(acww_runtime PUBLIC src)
set(ENGINE_COPIES)
foreach(n RANGE 0 17)
    add_library(gpu3d_tile${n} OBJECT src/gpu3d_tile.c)
    target_compile_definitions(gpu3d_tile${n} PRIVATE TILE=${n})
    list(APPEND ENGINE_COPIES gpu3d_tile${n})
endforeach()
foreach(n RANGE 0 2)
    add_library(gpu2d_probe${n} OBJECT src/gpu2d_probe.c)
    target_compile_definitions(gpu2d_probe${n} PRIVATE PROBE=${n})
    list(APPEND ENGINE_COPIES gpu2d_probe${n})
endforeach()
foreach(copy ${ENGINE_COPIES})
    target_include_directories(${copy} PRIVATE "${RUNTIME}" src)
    target_compile_options(${copy} PRIVATE -std=c11 -O2 -fwrapv -fno-strict-aliasing -w ${RUNTIME_INCLUDES})
    target_compile_definitions(${copy} PRIVATE _DEFAULT_SOURCE)
    target_sources(acww_runtime PRIVATE $<TARGET_OBJECTS:${copy}>)
endforeach()
if(NOT DECOMP_HOST)
    target_sources(acww_runtime PRIVATE src/native_os_portable.c)
endif()
target_include_directories(acww_runtime PUBLIC "${RUNTIME}")
# The decomp's flags (decomp/tools/run.py, build()); its sources are built as they are, with
# the port's compatibility header in front of each (port/compat/port_compat.h).
target_compile_options(acww_runtime PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-std=c11 -O2 -fwrapv -fno-strict-aliasing -w>
    "$<$<COMPILE_LANGUAGE:C>:${RUNTIME_INCLUDES}>")
target_compile_definitions(acww_runtime PRIVATE $<$<COMPILE_LANGUAGE:C>:_DEFAULT_SOURCE>)
set_source_files_properties(src/native_os_portable.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
target_include_directories(acww_runtime PRIVATE compat)
set(RUN_C_OPTIONS -std=c11 -O2 -fwrapv -fno-strict-aliasing -w ${RUNTIME_INCLUDES})
find_package(Threads REQUIRED)
target_link_libraries(acww_runtime PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})

# The ROM check and split, which the tests also run without a window.
add_library(acww_rom STATIC src/rom_prep.c src/sha1.c)
target_include_directories(acww_rom PUBLIC src)
target_compile_options(acww_rom PRIVATE -std=c11 -Wall -Wextra)
target_compile_definitions(acww_rom PRIVATE _DEFAULT_SOURCE)

# The decomp's headless program (run.c's own main) and the ROM split on their own, for the
# tests: port/tests/test_frames.py plays the decomp's recordings and compares the frames.
if(NOT ANDROID)
    add_executable(acww-run tools/run_headless.c "${RUNTIME}/sound.c" "${RUNTIME}/ds_audio.c")
    set_source_files_properties("${RUNTIME}/sound.c" "${RUNTIME}/ds_audio.c" PROPERTIES COMPILE_OPTIONS "${RUN_C_OPTIONS}")
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

# The Vulkan renderer's tool (docs/renderer.md): scenes from files drawn on this machine's
# Vulkan device. Built where the Vulkan headers are; the library is found when it runs.
find_path(VULKAN_HEADERS vulkan/vulkan.h)
if(VULKAN_HEADERS AND NOT ANDROID)
    add_executable(acww-scene-vk tools/scene_vk.c src/vk_scene.c src/scene_file.c)
    target_include_directories(acww-scene-vk PRIVATE src "${VULKAN_HEADERS}")
    target_compile_options(acww-scene-vk PRIVATE -std=c11 -Wall -Wextra)
    target_compile_definitions(acww-scene-vk PRIVATE _DEFAULT_SOURCE)
    target_link_libraries(acww-scene-vk PRIVATE ${CMAKE_DL_LIBS} m)
endif()

# run.c is built through run_port.c, which adds the port's hooks into the game's code behind
# the run's own (src/game_hooks.h)
set(FRONT_SOURCES src/front_sdl.c src/touch.c src/game_hooks.c src/autosave.c src/scene_file.c src/run_port.c
    src/sound_sdl.c src/ds_audio_port.c)
set_source_files_properties(src/run_port.c src/sound_sdl.c src/ds_audio_port.c PROPERTIES COMPILE_OPTIONS "${RUN_C_OPTIONS}")
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
# (they reach the DS's memory through memory_host too: the same memory.h in front of them)
set_source_files_properties(src/front_sdl.c src/touch.c src/game_hooks.c src/autosave.c PROPERTIES COMPILE_OPTIONS
    "-std=c11;-Wall;-Wextra;${RUNTIME_INCLUDES}")
target_compile_definitions(${FRONT} PRIVATE _DEFAULT_SOURCE PORT_VERSION="${PORT_VERSION}")
target_link_libraries(${FRONT} PRIVATE acww_rom acww_runtime ${SDL_TARGET} m)
