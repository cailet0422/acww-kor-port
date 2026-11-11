# Cross-compiling the headless test programs for Linux on aarch64 (gcc-aarch64-linux-gnu), to run under
# qemu-user: cmake -S port -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=port/cmake/linux-aarch64.cmake -DPORT_FRONT_END=OFF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
