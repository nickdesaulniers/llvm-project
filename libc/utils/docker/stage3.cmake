
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

# Use our freshly built libc++.
set(LLVM_ENABLE_LIBCXX ON CACHE BOOL "")

# For stage 1, just build clang and LLD, which are used by the "runtimes build"
# to build the runtime libraries of LLVM.
set(LLVM_ENABLE_PROJECTS "clang;lld;" CACHE STRING "")

# FORCE_ON causes the build to fail if zlib is not found in the environment
# during configuration, rather than much later during link.
# set(ZLIB_INCLUDE_DIR "/usr/include" CACHE STRING "")
# set(LLVM_ENABLE_ZLIB "FORCE_ON" CACHE STRING "")

# Just build stage1 to target the host. It's not the end product, so it won't
# be able to target all of the kernel targets we can build.
set(LLVM_TARGETS_TO_BUILD "host;" CACHE STRING "")

# Set clang's default -fuse-ld= to lld.
set(CLANG_DEFAULT_LINKER "lld" CACHE STRING "")

# Set clang's default --rtlib= to compiler-rt.
set(CLANG_DEFAULT_RTLIB "compiler-rt" CACHE STRING "")

# Disable arc migrate. We don't use that, ever.
set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "")

# Disable static analyzer. Don't need it for stage1.
set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "")

# Disable plugin support. Don't need it, ever.
set(CLANG_PLUGIN_SUPPORT OFF CACHE BOOL "")

# Work around lots of broken-ness in llvm.
set(LLVM_ENABLE_THREADS OFF CACHE BOOL "")
