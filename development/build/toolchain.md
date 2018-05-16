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

The example commands below use `${LLVM_SRCDIR}` to refer to the root of
your LLVM source tree checkout and assume the [monorepo
layout](https://llvm.org/docs/Proposals/GitHubMove.html#monorepo-variant).
When using this layout, each sub-project has its own top-level
directory.

The
[https://fuchsia.googlesource.com/third_party/llvm-project](https://fuchsia.googlesource.com/third_party/llvm-project)
repository emulates this layout via Git submodules and is updated
automatically by Gerrit. You can use the following command to download
this repository including all the submodules after setting the
`${LLVM_SRCDIR}` variable:

```bash
LLVM_SRCDIR=${HOME}/llvm-project
git clone --recurse-submodules https://fuchsia.googlesource.com/third_party/llvm-project ${LLVM_SRCDIR}
```

To update the repository including all the submodules, you can use:

```bash
git pull --recurse-submodules
```

Alternatively, you can use the semi-official monorepo
[https://github.com/llvm-project/llvm-project-20170507](https://github.com/llvm-project/llvm-project-20170507)
maintained by the LLVM community. This repository does not use
submodules which means you can use the standard Git workflow:

```bash
git clone https://github.com/llvm-project/llvm-project-20170507 ${LLVM_SRCDIR}
```

Before building the runtime libraries that are built along with the
toolchain, you need a Garnet SDK. We expect that the SDK is located in
the directory pointed to by the `${SDK_DIR}` variable:

```bash
SDK_DIR=${HOME}/sdk/garnet
```

To download the latest SDK, you can use the following:

```bash
./buildtools/cipd install fuchsia/sdk/linux-amd64 -version latest -root ${SDK_DIR}
```

Alternatively, you can build the Garnet SDK from source using the
following commands:

```bash
./scripts/build-zircon.sh

gn gen --args='target_cpu="x64" fuchsia_packages=["garnet/packages/sdk/garnet"]' out/x64
ninja -C out/x64

gn gen --args='target_cpu="arm64" fuchsia_packages=["garnet/packages/sdk/garnet"]' out/arm64
ninja -C out/arm64

./scripts/sdk/create_layout.py --manifest out/x64/gen/garnet/public/sdk/garnet_molecule.sdk --output ${SDK_DIR}
./scripts/sdk/create_layout.py --manifest out/arm64/gen/garnet/public/sdk/garnet_molecule.sdk --output ${SDK_DIR} --overlay
```

You can build a Fuchsia Clang compiler using the following commands.
These must be run in a separate build directory, which you must create.
This directory can be a subdirectory of `${LLVM_SRCDIR}` so that you
use `LLVM_SRCDIR=..` or it can be elsewhere, with `LLVM_SRCDIR` set
to an absolute or relative directory path from the build directory.

You need CMake version 3.8.0 and newer to execute these commands.
This was the first version to support Fuchsia.

```bash
cmake -GNinja \
  -DLLVM_ENABLE_PROJECTS=clang\;lldb\;lld \
  -DLLVM_ENABLE_RUNTIMES=compiler-rt\;libcxx\;libcxxabi\;libunwind \
  -DSTAGE2_FUCHSIA_x86_64_SYSROOT=${SDK_DIR}/arch/x64/sysroot \
  -DSTAGE2_FUCHSIA_x86_64_C_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DSTAGE2_FUCHSIA_x86_64_CXX_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DSTAGE2_FUCHSIA_x86_64_LINKER_FLAGS=-I${SDK_DIR}/arch/x64/lib \
  -DSTAGE2_FUCHSIA_aarch64_SYSROOT=${SDK_DIR}/arch/arm64/sysroot \
  -DSTAGE2_FUCHSIA_aarch64_C_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DSTAGE2_FUCHSIA_aarch64_CXX_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DSTAGE2_FUCHSIA_aarch64_LINKER_FLAGS=-I${SDK_DIR}/arch/arm64/lib \
  -C ${LLVM_SRCDIR}/clang/cmake/caches/Fuchsia.cmake \
  ${LLVM_SRCDIR}/llvm
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
your LLVM build directory). See the [instructions](#building-fuchsia)
on how to use the just built Clang to build Fuchsia.

*** note
**Note:** the second stage build uses LTO (Link Time Optimization) to
achieve better runtime performance of the final compiler. LTO often
requires a large amount of memory and is very slow. Therefore it may not
be very practical for day-to-day development.
***

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
-DLLVM_BUILTIN_TARGETS=x86_64-fuchsia\;aarch64-fuchsia \
-DBUILTINS_x86_64-fuchsia_CMAKE_SYSROOT=${FUCHSIA_x86_64_SYSROOT} \
-DBUILTINS_x86_64-fuchsia_CMAKE_SYSTEM_NAME=Fuchsia \
-DBUILTINS_aarch64-fuchsia_CMAKE_SYSROOT=${FUCHSIA_aarch64_SYSROOT} \
-DBUILTINS_aarch64-fuchsia_CMAKE_SYSTEM_NAME=Fuchsia
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
-DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
-DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
-DLLVM_ENABLE_LLD=ON
```

This assumes that `${CLANG_TOOLCHAIN_PREFIX}` points to the `bin` directory
of a Clang installation, with a trailing slash (as this Make variable is used
in the Zircon build).  For example, to use the compiler from your Fuchsia
checkout (on Linux):

```bash
CLANG_TOOLCHAIN_PREFIX=${HOME}/fuchsia/buildtools/linux-x64/clang/bin/
```

To build both builtins as well as runtimes (C++ library and sanitizer
runtimes), you can also use the cache file, but run only the second
stage, with LTO disabled, which gives you a faster build time suitable even
for incremental development, without having to manually specify all
options:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DLLVM_ENABLE_LTO=OFF \
  -DLLVM_ENABLE_PROJECTS=clang\;lldb\;lld \
  -DLLVM_ENABLE_RUNTIMES=compiler-rt\;libcxx\;libcxxabi\;libunwind \
  -DFUCHSIA_x86_64_SYSROOT=${SDK_DIR}/arch/x64/sysroot \
  -DFUCHSIA_x86_64_C_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DFUCHSIA_x86_64_CXX_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DFUCHSIA_x86_64_LINKER_FLAGS=-I${SDK_DIR}/arch/x64/lib \
  -DFUCHSIA_aarch64_SYSROOT=${SDK_DIR}/arch/arm64/sysroot \
  -DFUCHSIA_aarch64_C_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DFUCHSIA_aarch64_CXX_FLAGS=-I${SDK_DIR}/pkg/launchpad/include \
  -DFUCHSIA_aarch64_LINKER_FLAGS=-I${SDK_DIR}/arch/arm64/lib \
  -C ${LLVM_SRCDIR}/tools/clang/cmake/caches/Fuchsia-stage2.cmake \
  ${LLVM_SRCDIR}
ninja distribution
```

## Building Fuchsia with your custom Clang {#building-fuchsia}

You can start building test binaries right away by using the Clang in
`${LLVM_OBJDIR}/bin/`, or in `${LLVM_OBJDIR}/tools/clang/stage2-bins/bin/`
(depending on whether you did the two-stage build or the single-stage build,
the binaries will be in a different location). However, if you want to use
your Clang to build Fuchsia, you'll need to set some more arguments/variables.

If you're only interested in building Zircon, set the following environment
variables:

```bash
export USE_CLANG=true CLANG_TOOLCHAIN_PREFIX=${CLANG_DIR}
```

`${CLANG_DIR}` is the path to the `bin` directory for your Clang build,
e.g. `${LLVM_OBJDIR}/bin/`.

*** note
**Note:** that trailing slash is important.
***

Then run `fx build-zircon` as usual.

For layers-above-Zircon, it should be sufficient to pass
`--args clang_prefix="${CLANG_DIR}"` to `fx set`, then run `fx build` as usual.

Note that, since `fx full-build` implicitly builds Zircon, for a full build,
you also need to set the environment variables necessary for Zircon.

To ensure the environment variables are set every time you build, you may want
to run `fx set`, and then manually edit your `${FUCHSIA_SOURCE}/.config` file,
adding the following line:

```bash
export USE_CLANG=true CLANG_TOOLCHAIN_PREFIX=${LLVM_OBJDIR}/bin/
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
* [2017 LLVM Developers’ Meeting: Petr Hosek "Compiling cross-toolchains with CMake and runtimes build"](https://www.youtube.com/watch?v=OCQGpUzXDsY)
