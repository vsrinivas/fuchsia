# Toolchain

Fuchsia is using Clang as the official compiler.

## Building Clang

The Clang CMake build system supports bootstrap (aka multi-stage) builds. We use
two-stage bootstrap build for the Fuchsia Clang compiler.

The first stage compiler is a host-only compiler with some options set needed
for the second stage. The second stage compiler is the fully optimized compiler
intended to ship to users.

Setting up these compilers requires a lot of options. To simplify the
configuration the Fuchsia Clang build settings are contained in CMake cache
files which are part of the Clang codebase.

The example commands below use `${LLVM_SRCDIR}` to refer to the root of your
LLVM source tree checkout, and assume that the additional repositories are
checked out into their canonical subdirectories of the main LLVM checkout
(Clang in `tools/clang`, `compiler-rt` in `runtimes/compiler-rt`, etc.).  You
can cut & paste these commands into a shell after setting the `LLVM_SRCDIR`
variable, e.g.:

```bash
LLVM_SRCDIR=${HOME}/llvm
```

Before building the runtime libraries that are built along with the
compiler, you need a Zircon `sysroot` built.  This comes from the
Zircon build and sits in the `sysroot` subdirectory of the main Zircon
build directory.  For example, `.../build-zircon-pc-x86-64/sysroot`.
In the following commands, the string `${FUCHSIA_${arch}_SYSROOT}`
stands in for this absolute directory name.  You can cut & paste these
commands into a shell after setting the `FUCHSIA_${arch}_SYSROOT`
variable, e.g.:

```bash
make zircon-pc-x86-64
FUCHSIA_x86_64_SYSROOT=`pwd`/build-zircon-pc-x86-64/sysroot

make zircon-qemu-arm64
FUCHSIA_aarch64_SYSROOT=`pwd`/build-zircon-qemu-arm64/sysroot
```

You can build a Fuchsia Clang compiler using the following commands.
These must be run in a separate build directory, which you must create.
This directory can be a subdirectory of `${LLVM_SRCDIR}` so that you
use `LLVM_SRCDIR=..` or it can be elsewhere, with `LLVM_SRCDIR` set
to an absolute or relative directory path from the build directory.

```bash
cmake -G Ninja -DFUCHSIA_x86_64_SYSROOT=${FUCHSIA_x86_64_SYSROOT} -DFUCHSIA_aarch64_SYSROOT=${FUCHSIA_aarch64_SYSROOT} -C ${LLVM_SRCDIR}/tools/clang/cmake/caches/Fuchsia.cmake ${LLVM_SRCDIR}
ninja stage2-distribution
```

To install the compiler just built into `/usr/local`, you can use the
following command:

```bash
ninja stage2-install-distribution
```

To use the compiler just built without installing it into a system-wide
shared location, you can just refer to its build directory explicitly as
`${LLVM_OBJDIR}/tools/clang/stage2-bins/bin/` (where `LLVM_OBJDIR` is
your LLVM build directory).  For example, in a Zircon build, you can
pass the argument:

```bash
CLANG_TOOLCHAIN_PREFIX=${LLVM_OBJDIR}/tools/clang/stage2-bins/bin/
```

(Note: that trailing slash is important.)

You need CMake version 3.8.0 and newer to execute these commands.
This was the first version to support Fuchsia.

Note that the second stage build uses LTO (Link Time Optimization) to achieve
better runtime performance of the final compiler. LTO often requires a large
amount of memory and is very slow. Therefore it may not be very practical for
day-to-day development.

## Developing Clang

When developing Clang, you may want to use a setup that is more suitable for
incremental development and fast turnaround time.

The simplest way to build is to use the following commands:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ${LLVM_SRCDIR}
ninja
```

To also build compiler builtins (a library that provides an implementation of
the low-level target-specific routines) for Fuchsia, you need a few additional
flags:

```bash
-DLLVM_BUILTIN_TARGETS='x86_64-fuchsia-none;aarch64-fuchsia-none' -DBUILTINS_x86_64-fuchsia-none_CMAKE_SYSROOT=${FUCHSIA_x86_64_SYSROOT} -DBUILTINS_x86_64-fuchsia-none_CMAKE_SYSTEM_NAME=Fuchsia -DBUILTINS_aarch64-fuchsia-none_CMAKE_SYSROOT=${FUCHSIA_aarch64_SYSROOT} -DBUILTINS_aarch64-fuchsia-none_CMAKE_SYSTEM_NAME=Fuchsia
```

For this kind of build, the `bin` directory immediate under your main LLVM
build directory contains the compiler binaries.  You can put that directory
into your shell's `PATH`, or use it explicitly in commands, or use it in the
`CLANG_TOOLCHAIN_PREFIX` variable (with trailing slash) for a Zircon build.

Clang is a large project and compiler performance is absolutely critical. To
reduce the build time, we recommend using Clang as a host compiler, and if
possible, LLD as a host linker. These should be ideally built using LTO and
for best possible performance also using Profile-Guided Optimizations (PGO).

To set the host compiler, you can use the following extra flags:

```bash
-DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ -DLLVM_ENABLE_LLD=ON
```

This assumes that `${CLANG_TOOLCHAIN_PREFIX}` points to the `bin` directory
of a Clang installation, with a trailing slash (as this Make variable is used
in the Zircon build).  For example, to use the compiler from your Fuchsia
checkout (on Linux):

```bash
CLANG_TOOLCHAIN_PREFIX=${HOME}/fuchsia/buildtools/toolchain/clang+llvm-x86_64-linux/bin/
```

To build both builtins as well as runtimes (C++ library and sanitizer
runtimes), you can also use the cache file, but run only the second
stage, with LTO disabled, which gives you a faster build time suitable even
for incremental development, without having to manually specify all
options:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ -DLLVM_ENABLE_LTO=OFF -DFUCHSIA_x86_64_SYSROOT=${FUCHSIA_x86_64_SYSROOT} -DFUCHSIA_aarch64_SYSROOT=${FUCHSIA_aarch64_SYSROOT} -C ${LLVM_SRCDIR}/tools/clang/cmake/caches/Fuchsia-stage2.cmake ${LLVM_SRCDIR}
```

## Building sanitized versions of LLVM tools

Most sanitizers can be used on LLVM tools by adding
`LLVM_USE_SANITIZER=<sanitizer name>` to your cmake invocation. MSan is
special however because some llvm tools trigger false positives. To build with
MSan support you first need to build libc++ with MSan support. You can do this
in the same build. To set up a build with MSan support first run CMake with
`LLVM_USE_SANITIZER=Memory` and `LLVM_ENABLE_LIBCXX=ON`.

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_USE_SANITIZER=Memory -DLLVM_ENABLE_LIBCXX=ON -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ -DLLVM_ENABLE_LLD=ON ${LLVM_SRCDIR}
```

Normally you would run Ninja at this point but we want to build everything
using a sanitized version of libc++ but if we build now it will use libc++ from
`${CLANG_TOOLCHAIN_PREFIX}` which isn't sanitized. So first we build just
the cxx and cxxabi targets. These will be used in place of the ones from
`${CLANG_TOOLCHAIN_PREFIX}` when tools dynamically link against libcxx.

```bash
ninja cxx cxxabi
```

Now that we have a sanitized version of libc++ we can have our build use it
instead of the one from `${CLANG_TOOLCHAIN_PREFIX}` and then build everything.

```bash
ninja
```

Putting that all together:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_USE_SANITIZER=Address -DLLVM_ENABLE_LIBCXX=ON -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ -DLLVM_ENABLE_LLD=ON ${LLVM_SRCDIR}
ninja libcxx libcxxabi
ninja
```

## Additional Resources

Documentation:
* [Getting Started with the LLVM System](http://llvm.org/docs/GettingStarted.html)
* [Building LLVM with CMake](http://llvm.org/docs/CMake.html)
* [Advanced Build Configurations](http://llvm.org/docs/AdvancedBuilds.html)

Talks:
* [2016 LLVM Developers’ Meeting: C. Bieneman "Developing and Shipping LLVM and Clang with CMake"](https://www.youtube.com/watch?v=StF77Cx7pz8)
