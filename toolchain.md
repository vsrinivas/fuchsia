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
files which are part of the Clang codebase. You can build a Fuchsia Clang
compiler using the following commands:

```bash
$ cmake -G Ninja -DFUCHSIA_SYSROOT=<path to magenta>/third_party/ulib/musl -C <path to clang>/cmake/caches/Fuchsia.cmake <path to source>
$ ninja stage2-distribution
```

To install the just built compiler, you can use the following command:

```bash
$ ninja stage2-install-distribution
```

You need CMake version 3.8.0 and newer to execute these commands which was the
first version that has support for Fuchsia.

Note that the second stage build uses LTO (Link Time Optimization) to achieve
better runtime performance of the final compiler. LTO often requires a large
amount of memory and is very slow. Therefore it may not be very practical for
day-to-day development.

## Developing Clang

When developing Clang, you may want to use a setup that is more suitable for
incremental development and fast turnaround time.

The simplest way to build is to use the following commands:

```bash
$ cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug <path to source>
$ ninja
```

To also build compiler builtins (a library that provides an implementation of
the low-level target-specific routines) for Fuchsia, you need a few additional
flags:

```bash
-DLLVM_BUILTIN_TARGETS="x86_64-fuchsia-none;aarch64-fuchsia-none" -DBUILTINS_x86_64-fuchsia-none_CMAKE_SYSROOT=<path to magenta>/third_party/ulib/musl -DBUILTINS_x86_64-fuchsia-none_CMAKE_SYSTEM_NAME=Fuchsia -DBUILTINS_aarch64-fuchsia-none_CMAKE_SYSROOT=<path to magenta>/third_party/ulib/musl -DBUILTINS_aarch64-fuchsia-none_CMAKE_SYSTEM_NAME=Fuchsia
```

Clang is a large project and compiler performance is absolutely critical. To
reduce the build time, we recommend using Clang as a host compiler, and if
possible, LLD as a host linker. These should be ideally built using LTO and
for best possible performance also using Profile-Guided Optimizations (PGO).

To set the host compiler, you can use the following extra flags:

```bash
-DCMAKE_C_COMPILER=<path to toolchain>/clang -DCMAKE_CXX_COMPILER=<path to toolchain>/clang++ -DLLVM_ENABLE_LLD=ON
```

## Additional Resources

Documentation:
* [Getting Started with the LLVM System](http://llvm.org/docs/GettingStarted.html)
* [Building LLVM with CMake](http://llvm.org/docs/CMake.html)
* [Advanced Build Configurations](http://llvm.org/docs/AdvancedBuilds.html)

Talks:
* [2016 LLVM Developers’ Meeting: C. Bieneman "Developing and Shipping LLVM and Clang with CMake"](https://www.youtube.com/watch?v=StF77Cx7pz8)
