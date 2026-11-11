# Cross-compiling the headless test programs for Linux on armhf (gcc-arm-linux-gnueabihf), to run under
# qemu-user: cmake -S port -B build-armhf -DCMAKE_TOOLCHAIN_FILE=port/cmake/linux-armhf.cmake -DPORT_FRONT_END=OFF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
