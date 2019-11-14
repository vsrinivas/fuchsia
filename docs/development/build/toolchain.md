# Toolchain

Fuchsia is using Clang as the official compiler.

## Prerequisites

You need [CMake](https://cmake.org/download/) version 3.8.0 and newer to
execute these commands. This was the first version to support Fuchsia.

While CMake supports different build systems, we recommend using
[Ninja](https://github.com/ninja-build/ninja/releases) which installed
to be present on your system.

## Getting Source

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

Alternatively, you can use the official monorepo
[https://github.com/llvm/llvm-project](https://github.com/llvm/llvm-project)
maintained by the LLVM community. This repository does not use
submodules which means you can use the standard Git workflow:

```bash
git clone https://github.com/llvm/llvm-project ${LLVM_SRCDIR}
```

### Fuchsia SDK

Before building the runtime libraries that are built along with the
toolchain, you need a Fuchsia SDK. We expect that the SDK is located in
the directory pointed to by the `${SDK_DIR}` variable:

```bash
SDK_DIR=${HOME}/fuchsia/sdk
```

To download the latest SDK, you can use the following:

```bash
cipd install fuchsia/sdk/core/linux-amd64 latest -root ${SDK_DIR}
```

## Building Clang

The Clang CMake build system supports bootstrap (aka multi-stage)
builds. We use two-stage bootstrap build for the Fuchsia Clang compiler.

The first stage compiler is a host-only compiler with some options set
needed for the second stage. The second stage compiler is the fully
optimized compiler intended to ship to users.

Setting up these compilers requires a lot of options. To simplify the
configuration the Fuchsia Clang build settings are contained in CMake
cache files which are part of the Clang codebase.

You can build Clang toolchain for Fuchsia using the following commands.
These must be run in a separate build directory, which you must create.
This directory can be a subdirectory of `${LLVM_SRCDIR}` so that you
use `LLVM_SRCDIR=..` or it can be elsewhere, with `LLVM_SRCDIR` set
to an absolute or relative directory path from the build directory.

```bash
cmake -GNinja \
  -DLLVM_ENABLE_PROJECTS="clang;lld;clang-tools-extra" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind" \
  -DSTAGE2_FUCHSIA_SDK=${SDK_DIR} \
  -C ${LLVM_SRCDIR}/clang/cmake/caches/Fuchsia.cmake \
  ${LLVM_SRCDIR}/llvm
ninja stage2-distribution
```

To include compiler runtimes and C++ library for Linux, you need to use
`LINUX_<architecture>_SYSROOT` flag to point at the sysroot and specify
the correct host triple. For example, to build the runtimes for
`x86_64-unknown-linux-gnu` using the sysroot from your Fuchsia checkout, you
would use:

```bash
  -DSTAGE2_LINUX_x86_64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-x64 \
  -DSTAGE2_LINUX_aarch64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-arm64 \
```

To install the compiler just built into `/usr/local`, you can use the
following command:

```bash
ninja stage2-install-distribution
```

To use the compiler just built without installing it into a system-wide
shared location, you can just refer to its build directory explicitly as
`${LLVM_OBJDIR}/tools/clang/stage2-bins/bin/` (where `LLVM_OBJDIR` is
your LLVM build directory).

Note: the second stage build uses LTO (Link Time Optimization) to
achieve better runtime performance of the final compiler. LTO often
requires a large amount of memory and is very slow. Therefore it may not
be very practical for day-to-day development.

Note: If the Fuchsia build fails due to missing `runtime.json`,
`aarch64-fuchsia.manifest`, or `x86_64-fuchsia.manifest` files, you can copy
them over from the prebuilt toolchain.

## Developing Clang

When developing Clang, you may want to use a setup that is more suitable for
incremental development and fast turnaround time.

The simplest way to build LLVM is to use the following commands:

```bash
cmake -GNinja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
  ${LLVM_SRCDIR}/llvm
ninja
```

You can enable additional projects using the `LLVM_ENABLE_PROJECTS`
variable. To enable all common projects, you would use:

```bash
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;compiler-rt;libcxx;libcxxabi;libunwind"
```

Similarly, you can also enable some projects to be built as runtimes
which means these projects will be built using the just-built rather
than the host compiler:

```bash
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind" \
```

Clang is a large project and compiler performance is absolutely critical. To
reduce the build time, we recommend using Clang as a host compiler, and if
possible, LLD as a host linker. These should be ideally built using LTO and
for best possible performance also using Profile-Guided Optimizations (PGO).

To set the host compiler to Clang and the host linker to LLD, you can
use the following extra flags:

```bash
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DLLVM_ENABLE_LLD=ON
```

This assumes that `${CLANG_TOOLCHAIN_PREFIX}` points to the `bin` directory
of a Clang installation, with a trailing slash (as this Make variable is used
in the Zircon build). For example, to use the compiler from your Fuchsia
checkout (on Linux):

```bash
CLANG_TOOLCHAIN_PREFIX=${FUCHSIA}/prebuilt/third_party/clang/linux-x64/bin/
```

Note: To build Fuchsia, you need a stripped version of the toolchain runtime
binaries. Use `DESTDIR=/path/to/install/dir ninja install-distribution-stripped`
to get a stripped install and then point your build configuration to
`/path/to/install/dir/bin` as your toolchain.

### Sanitizers

Most sanitizers can be used on LLVM tools by adding
`LLVM_USE_SANITIZER=<sanitizer name>` to your cmake invocation. MSan is
special however because some LLVM tools trigger false positives. To
build with MSan support you first need to build libc++ with MSan
support. You can do this in the same build. To set up a build with MSan
support first run CMake with `LLVM_USE_SANITIZER=Memory` and
`LLVM_ENABLE_LIBCXX=ON`.

```bash
cmake -GNinja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;libcxx;libcxxabi;libunwind" \
  -DLLVM_USE_SANITIZER=Memory \
  -DLLVM_ENABLE_LIBCXX=ON \
  -DLLVM_ENABLE_LLD=ON \
  ${LLVM_SRCDIR}/llvm
```

Normally you would run Ninja at this point but we want to build
everything using a sanitized version of libc++ but if we build now it
will use libc++ from `${CLANG_TOOLCHAIN_PREFIX}` which isn't sanitized.
So first we build just the cxx and cxxabi targets. These will be used in
place of the ones from `${CLANG_TOOLCHAIN_PREFIX}` when tools
dynamically link against libcxx

```bash
ninja cxx cxxabi
```

Now that we have a sanitized version of libc++ we can have our build use
it instead of the one from `${CLANG_TOOLCHAIN_PREFIX}` and then build
everything.

```bash
ninja
```

Putting that all together:

```bash
cmake -GNinja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DLLVM_USE_SANITIZER=Address \
  -DLLVM_ENABLE_LIBCXX=ON \
  -DLLVM_ENABLE_LLD=ON \
  ${LLVM_SRCDIR}/llvm
ninja libcxx libcxxabi
ninja
```

### [Googlers only] Goma

Ensure Goma is installed on your machine for faster builds; Goma
accelerates builds by distributing compilation across many machines. If
you have Goma installed in `${GOMA_DIR}` (by default `${HOME}/goma`),
you can enable Goma use with the following extra flags:

```bash
  -DCMAKE_C_COMPILER_LAUNCHER=${GOMA_DIR}/gomacc \
  -DCMAKE_CXX_COMPILER_LAUNCHER=${GOMA_DIR}/gomacc \
  -DLLVM_PARALLEL_LINK_JOBS=${LINK_JOBS}
```

The number of link jobs is dependent on RAM size, for LTO build you will
need at least 10GB for each job.

To build Clang with Goma, use:

```bash
ninja -j${JOBS}
```

Use `-j100` for Goma on macOS and `-j1000` for Goma on Linux. You may
need to tune the job count to suit your particular machine and workload.

Note: that in order to use Goma, you need a host compiler that is
supported by Goma such as the Fuchsia Clang installation. See above on
how to configure your LLVM buile to use a different host compiler.

To verify your compiler is available on Goma, you can set
`GOMA_USE_LOCAL=0 GOMA_FALLBACK=0` environment variables. If the
compiler is not available, you will see an error.

### Fuchsia Configuration

When developing Clang for Fuchsia, you can also use the cache file to
test the Fuchsia configuration, but run only the second stage, with LTO
disabled, which gives you a faster build time suitable even for
incremental development, without having to manually specify all options:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DLLVM_ENABLE_LTO=OFF \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind" \
  -DLINUX_x86_64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-x64 \
  -DLINUX_aarch64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-arm64 \
  -DFUCHSIA_SDK=${SDK_DIR} \
  -C ${LLVM_SRCDIR}/clang/cmake/caches/Fuchsia-stage2.cmake \
  ${LLVM_SRCDIR}/llvm
ninja distribution
```

With Goma for even faster turnaround time:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang \
  -DCMAKE_CXX_COMPILER=${CLANG_TOOLCHAIN_PREFIX}clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=${GOMA_DIR}/gomacc \
  -DCMAKE_CXX_COMPILER_LAUNCHER=${GOMA_DIR}/gomacc \
  -DCMAKE_EXE_LINKER_FLAGS="-ldl -lpthread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-ldl -lpthread" \
  -DLLVM_PARALLEL_LINK_JOBS=${LINK_JOBS} \
  -DLLVM_ENABLE_LTO=OFF \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind" \
  -DLINUX_x86_64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-x64 \
  -DLINUX_aarch64-unknown-linux-gnu_SYSROOT=${FUCHSIA}/prebuilt/third_party/sysroot/linux-arm64 \
  -DFUCHSIA_SDK=${SDK_DIR} \
  -C ${LLVM_SRCDIR}/clang/cmake/caches/Fuchsia-stage2.cmake \
  ${LLVM_SRCDIR}/llvm
ninja distribution -j${JOBS}
```

## Testing Clang

To run Clang tests, you can use the `check-<component>` target:

```
ninja check-llvm check-clang
```

You can all use `check-all` to run all tests, but keep in mind that this
can take significant amount of time depending on the number of projects
you have enabled in your build.

### Building Fuchsia with custom Clang locally

You can start building test binaries right away by using the Clang in
`${LLVM_OBJDIR}/bin/`, or in
`${LLVM_OBJDIR}/tools/clang/stage2-bins/bin/` (depending on whether you
did the two-stage build or the single-stage build, the binaries will be
in a different location). However, if you want to use your Clang to
build Fuchsia, you will need to set some more arguments/variables.

If you are only interested in building Zircon, set the following
GN build arguments:

```bash
gn gen build-zircon --args='variants = [ "clang" ] clang_tool_dir = ${CLANG_DIR}'
```

`${CLANG_DIR}` is the path to the `bin` directory for your Clang build,
e.g. `${LLVM_OBJDIR}/bin/`.

Note: that trailing slash is important.

Then run `fx build-zircon` as usual.

For layers-above-Zircon, it should be sufficient to pass
`--args clang_prefix="${CLANG_DIR}"` to `fx set`, then run `fx build` as usual.

### Building Fuchsia with custom Clang on bots (Googlers only)

Fuchsia's infrastructure has support for using a non-default version of Clang
to build. Only Clang instances that have been uploaded to CIPD or Isolate are
available for this type of build, and so any local changes must land in
upstream and be built by the CI or production toolchain bots.

You will need the infra codebase and prebuilts. Directions for checkout are on
the infra page.

To trigger a bot build with a specific revision of Clang, you will need the Git
revision of the Clang with which you want to build. This is on the [CIPD page](https://chrome-infra-packages.appspot.com/p/fuchsia/clang),
or can be retrieved using the CIPD CLI. You can then run the following command:

```bash
export FUCHSIA_SOURCE=<path_to_fuchsia>
export BUILDER=<builder_name>
export REVISION=<clang_revision>

export INFRA_PREBUILTS=${FUCHSIA_SOURCE}/fuchsia-infra/prebuilt/tools

cd ${FUCHSIA_SOURCE}/fuchsia-infra/recipes

${INFRA_PREBUILTS}/led get-builder 'luci.fuchsia.ci:${BUILDER}' | \
${INFRA_PREBUILTS}/led edit-recipe-bundle -O | \
jq '.userland.recipe_properties."$infra/fuchsia".clang_toolchain.type="cipd"' | \
jq '.userland.recipe_properties."$infra/fuchsia".clang_toolchain.instance="git_revision:${REVISION}"' | \
${INFRA_PREBUILTS}/led launch
```

It will provide you with a link to the BuildBucket page to track your build.

You will need to run `led auth-login` prior to triggering any builds, and may need to
file an infra ticket to request access to run led jobs.

## Additional Resources

Documentation:

* [Getting Started with the LLVM System](http://llvm.org/docs/GettingStarted.html)
* [Building LLVM with CMake](http://llvm.org/docs/CMake.html)
* [Advanced Build Configurations](http://llvm.org/docs/AdvancedBuilds.html)

Talks:

* [2016 LLVM Developers’ Meeting: C. Bieneman "Developing and Shipping LLVM and Clang with CMake"](https://www.youtube.com/watch?v=StF77Cx7pz8)
* [2017 LLVM Developers’ Meeting: Petr Hosek "Compiling cross-toolchains with CMake and runtimes build"](https://www.youtube.com/watch?v=OCQGpUzXDsY)
