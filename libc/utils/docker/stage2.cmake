# Please try to keep these cmake variables alphabetically sorted.

# Enable optimizations, as opposed to a debug build.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")

set(CMAKE_INSTALL_PREFIX "/sysroot/usr/" CACHE STRING "")

# Use Alpine's clang and clang++ from their clang package as the stage0
# compilers.
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE FILEPATH "")
set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE FILEPATH "")

# Use Alpine's lld as the stage0 linker to link everything.
set(LLVM_ENABLE_LLD ON CACHE BOOL "")

# For stage 1, just build clang and LLD, which are used by the "runtimes build"
# to build the runtime libraries of LLVM.
# set(LLVM_ENABLE_PROJECTS "clang;lld;" CACHE STRING "")

# LLVM "runtimes" build.
set(LLVM_ENABLE_RUNTIMES "libcxxabi;libcxx" CACHE STRING "")

# Disable support for unwinding. Thus we don't need to build llvm's libunwind.
set(LIBCXXABI_ENABLE_EXCEPTIONS OFF CACHE BOOL "")

# This is needed to break the cycle between libc, libc++, and libunwind.
# set(LIBCXXABI_ENABLE_STATIC_UNWINDER ON CACHE BOOL "")

# We don't plan to run the tests; don't build them.
set(LIBCXXABI_INCLUDE_TESTS OFF CACHE BOOL "")

# We want libc++abi to use compiler-rt.
set(LIBCXXABI_USE_COMPILER_RT ON CACHE BOOL "")

# Disabling LIBCXXABI_ENABLE_EXCEPTIONS doesn't imply disabling
# LIBCXXABI_USE_LLVM_UNWINDER for some god forsaken reason...
set(LIBCXXABI_USE_LLVM_UNWINDER OFF CACHE BOOL "")

# Disable usage of pthreads. llvm-libc may be able to support C11 threads, if
# libc++abi did. https://github.com/llvm/llvm-project/issues/124010
set(LIBCXXABI_ENABLE_THREADS OFF CACHE BOOL "")

# Disable demangler, depends on wcs support.
set(LIBCXXABI_NON_DEMANGLING_TERMINATE ON CACHE BOOL "")

# This will link against musl libc.so.
set(LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "")

# https://libcxx.llvm.org/VendorDocumentation.html#general-purpose-options

# We want libc++ to use libc++abi.
set(LIBCXX_CXX_ABI libcxxabi CACHE BOOL "")

# We need to disable this otherwise libc++abi pulls in libc++'s <algorithm>
# which will pull in pstl, which needs pthreads...
set(LIBCXX_ENABLE_EXPERIMENTAL_LIBRARY OFF CACHE BOOL "")

# Disable filesystem support. Requires mbrlen, mbsrtowcs
set(LIBCXX_ENABLE_FILESYSTEM OFF CACHE BOOL "")

# We want libc++ to use compiler-rt. If we don't disable libatomic explicitly,
# libc++ will try to link against it.
# set(LIBCXX_HAS_ATOMIC_LIB OFF CACHE BOOL "")
# set(LIBCXX_HAS_GCC_LIB OFF CACHE BOOL "")
# set(LIBCXX_HAS_GCC_S_LIB OFF CACHE BOOL "")

# The C++ standard library requires the C library. libc++ assumes that the
# default C library is glibc due to the prevalence, however, as we are using
# musl, we need to indicate that we are using musl to prevent failures due to
# incorrect assumptions, particularly about locales.
# set(LIBCXX_HAS_MUSL_LIBC ON CACHE BOOL "")

# We don't plan to run the benchmarks, so don't build them.
# set(LIBCXX_INCLUDE_BENCHMARKS OFF CACHE BOOL "")

# Don't build docs.
# set(LIBCXX_INCLUDE_DOCS OFF CACHE BOOL "")

# Don't build tests.
# set(LIBCXX_INCLUDE_TESTS OFF CACHE BOOL "")

# We want libc++ to use compiler-rt.
# set(LIBCXX_USE_COMPILER_RT ON CACHE BOOL "")

# Disable wide char support.
set(LIBCXX_ENABLE_WIDE_CHARACTERS OFF CACHE BOOL "")
# Disable <iostream>, <regex>, <locale>, since they depend on strtoll_l
set(LIBCXX_ENABLE_LOCALIZATION OFF CACHE BOOL "")

# Avoid pthreads.
set(LIBCXX_ENABLE_THREADS OFF CACHE BOOL "")
# Avoid ioctl.
set(LIBCXX_ENABLE_RANDOM_DEVICE OFF CACHE BOOL "")

# Avoid linkage failures on __gxx_personality_v0, __cxa_begin_catch,
# __cxa_rethrow, __cxa_allocate_exception, __cxa_throw, __cxa_free_exception,
# __cxa_end_catch
set(LIBCXX_ENABLE_EXCEPTIONS OFF CACHE BOOL "")

# avoid linkage failure against __llvm_libc_errno
set(LIBCXX_ENABLE_TIME_ZONE_DATABASE OFF CACHE BOOL "")

# This will try to link against libc.so (musl).
set(LIBCXX_ENABLE_SHARED OFF CACHE BOOL "")
# This will try to link against lib/libc++abi.so.1.0, which was linked against
# musl libc.so.
set(LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "")

# Defaults to include/c++/v1
# set(LIBCXX_INSTALL_INCLUDE_DIR "include" CACHE STRING "")
