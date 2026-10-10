set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(BUILDROOT_DIR
    "/working/stm32mp2/build/rootfs")

set(BUILDROOT_HOST
    "${BUILDROOT_DIR}/output/host")

set(BUILDROOT_SYSROOT
    "${BUILDROOT_DIR}/output/staging")


# ------------------------------------------------------------
# Cross compiler
# ------------------------------------------------------------

set(CMAKE_C_COMPILER
    "${BUILDROOT_HOST}/bin/aarch64-linux-gcc")

set(CMAKE_CXX_COMPILER
    "${BUILDROOT_HOST}/bin/aarch64-linux-g++")

set(CMAKE_SYSROOT
    "${BUILDROOT_SYSROOT}")


# ------------------------------------------------------------
# Target search paths
# ------------------------------------------------------------

set(CMAKE_FIND_ROOT_PATH
    "${BUILDROOT_SYSROOT}"
    "${BUILDROOT_HOST}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


# ------------------------------------------------------------
# pkg-config
# ------------------------------------------------------------

set(ENV{PKG_CONFIG_SYSROOT_DIR}
    "${BUILDROOT_SYSROOT}")

set(ENV{PKG_CONFIG_LIBDIR}
    "${BUILDROOT_SYSROOT}/usr/lib/pkgconfig:${BUILDROOT_SYSROOT}/usr/share/pkgconfig")

# Do NOT inherit host PKG_CONFIG_PATH.
set(ENV{PKG_CONFIG_PATH} "")

set(ENV{PKG_CONFIG_ALLOW_SYSTEM_CFLAGS} "1")
set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS} "1")
