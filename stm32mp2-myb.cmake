set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(SDK_DIR
    "/working/stm32mp2/toolchains/sysroots")

set(SDK_HOST
    "${SDK_DIR}/x86_64-ostl_sdk-linux/usr/bin/aarch64-ostl-linux/")

set(SDK_SYSROOT
    "${SDK_DIR}/cortexa35-ostl-linux")


# ------------------------------------------------------------
# Cross compiler
# ------------------------------------------------------------

set(CMAKE_C_COMPILER
    "${SDK_HOST}/aarch64-ostl-linux-gcc")

set(CMAKE_CXX_COMPILER
    "${SDK_HOST}/aarch64-ostl-linux-g++")

set(CMAKE_SYSROOT
    "${SDK_SYSROOT}")


# ------------------------------------------------------------
# Target search paths
# ------------------------------------------------------------

set(CMAKE_FIND_ROOT_PATH
    "${SDK_SYSROOT}"
    "${SDK_HOST}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


# ------------------------------------------------------------
# pkg-config
# ------------------------------------------------------------

set(ENV{PKG_CONFIG_SYSROOT_DIR}
    "${SDK_SYSROOT}")

set(ENV{PKG_CONFIG_LIBDIR}
    "${SDK_SYSROOT}/usr/lib/pkgconfig:${SDK_SYSROOT}/usr/share/pkgconfig")

# Do NOT inherit host PKG_CONFIG_PATH.
set(ENV{PKG_CONFIG_PATH} "")

set(ENV{PKG_CONFIG_ALLOW_SYSTEM_CFLAGS} "1")
set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS} "1")
