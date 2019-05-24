
Contributing to FIDL
====================

[TOC]

## Overview

The [FIDL](README.md) toolchain is composed of roughly three parts:

1. Front-end, a.k.a. `fidlc`
    *   Parses and validates `.fidl` files
    *   Calculates size, alignment, and offset of various structures
    *   Produces a [JSON IR][jsonir] (Intermediate Representation)
2. Back-end
    *   Works off the IR (except the C back-end)
    *   Produces target language specific code, which ties into the libraries for that language
3. Runtime Libraries
    *   Implement encoding/decoding/validation of messages
    *   Method dispatching mechanics

### Code Location

The front-end lives at [//zircon/system/host/fidl/][fidlc-source],
with tests in [//zircon/system/utest/fidl/][fidlc-tests].

The back-end and runtime library locations are based on the target:

Target    | Back-end                                               | Runtime Libraries
----------|--------------------------------------------------------|------------------
C         | [//zircon/system/host/fidl/lib/c_generator.cpp][be-c]  | [//zircon/system/ulib/fidl/][rtl-c]
C++       | [//garnet/go/src/fidl/compiler/backend/cpp/][be-cpp]   | [//zircon/system/ulib/fidl/][rtl-c] & [//garnet/public/lib/fidl/cpp/][rtl-cpp]
Go        | [//garnet/go/src/fidl/compiler/backend/golang/][be-go] | [//third_party/go/src/syscall/zx/fidl/][rtl-go]
Rust      | [//garnet/go/src/fidl/compiler/backend/rust/][be-rust] | [//garnet/public/lib/fidl/rust/fidl/][rtl-rust]
Dart      | [//topaz/bin/fidlgen_dart/][be-dart]                   | [//topaz//public/dart/fidl/][rtl-dart]<br>[//topaz/bin/fidl_bindings_test/][bindings_test-dart]
JavaScipt | [chromium:build/fuchsia/fidlgen_fs][be-js]             | [chromium:build/fuchsia/fidlgen_js/runtime][rtl-js]

### Other Tools

**TBD: linter, formatter, gidl, difl, regen scripts, etc.**

## General Setup

### Fuchsia Setup

Read the [Fuchsia Getting Started][getting_started] guide first.

### fx set

```sh
fx set core.x64 --with //bundles:tests --with //topaz/packages/tests:all --with //sdk:modular_testing
```

or, to ensure there's no breakage with lots of bindings etc.:

```sh
fx set terminal.x64 --with //bundles:kitchen_sink --with //vendor/google/bundles:buildbot
```

## Compiling, and Running Tests

We provide mostly one-liners to run tests for the various parts.
When in doubt, refer to the "`Test:`" comment in the git commit message;
we do our best to describe the commands used to validate our work there.

### fidlc

In `zircon/`:

```sh
./scripts/gn gen build-zircon --args='variants = ["host_asan"]'
./scripts/ninja -C build-zircon tools
./scripts/ninja -C build-zircon system/utest:host
```

Tests are now built as:

```sh
./build-zircon/host-x64-linux-clang/obj/system/utest/fidl-compiler/fidl-compiler-test.debug
./build-zircon/host-x64-mac-clang/obj/system/utest/fidl-compiler/fidl-compiler-test.debug
```

### fidl-coding-table

```sh
./scripts/build-zircon-x64 && \
./scripts/run-zircon-x64 -c zircon.autorun.boot=/boot/test/sys/fidl-coding-tables-test
```

### fidlgen

Some example tests you can run:

```sh
fx run-host-tests fidlgen_cpp_ir_test
fx run-host-tests fidlgen_golang_ir_test
```

### C runtime

In `zircon/`:

```sh
make && ./scripts/run-zircon -a x64 -c zircon.autorun.boot=/boot/test/sys/fidl-test
```

When the test completes, you're running in the QEMU emulator.
To exit, use **`Ctrl-A x`**.

### C++ runtime

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx run -kN

Tab 3> fx run-test fidl_tests
```

### Go runtime

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx run -kN

Tab 3> fx run-test go_fidl_tests
```

As with normal Go tests, you can pass [various flags][go-test-flags] to control
execution, filter test cases, run benchmarks, etc. For instance:

```sh
Tab 3> fx run-test go_fidl_tests-- -test.v -test.run 'TestAllSuccessCases/.*xunion.*'
```

### Rust runtime

**TBD**

### Dart runtime

The Dart FIDL bindings tests are in [//topaz/bin/fidl_bindings_test/][bindings_test-dart].

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx run -kN

Tab 3> fx run-test fidl_bindings_test
```

### Compatibility Test

The language bindings compatibility test is located in
[//topaz/bin/fidl_compatibility_test][compatibility_test],
and is launched from a shell script.

To build this test, use:

```sh
fx build topaz/bin/fidl_compatibility_test/dart:fidl_compatibility_test_server_dart
``

To run this test, you first need to have a Fuchsia instance running in an emulator:

```sh
fx run -N
```

Then, copy the script to the device, and run it:

```sh
fx cp `find topaz -name run_fidl_compatibility_test_topaz.sh` /tmp/
fx shell /tmp/run_fidl_compatibility_test_topaz.sh
```

## Workflows

### Language evolutions

One common task is to evolve the language, or introduce stricter checks in `fidlc`.
These changes typically follow a three phase approach:

1. Write the new compiler code in `fidlc`;
2. Use this updated `fidlc` to compile all layers,
   including vendor/google, make changes as needed;
3. When all is said and done, the `fidlc` changes can finally be merged.

All of this assumes that (a) code which wouldn't pass the new checks, or (b) code
that has new features, is *not* introduced concurrently between step 2 and step 3.
That typically is the case, however, it is ok to deal with breaking rollers
once in a while.

### Go fuchsia.io and fuchsia.net

To update all the saved `fidlgen` files, run the following command,
which automatically searches for and generates the necessary go files:

```sh
fx exec third_party/go/regen-fidl
```

## FAQs

### Why is the C back-end different than all other back-ends?

TBD

### Why is fidlc in the zircon repo?

TBD

### Why aren't all back-ends in one tool?

We'd actually like all back-ends to be in _separate_ tools!

Down the road, we plan to have a script over all the various tools (`fidlc`,
`fidlfmt`, the various back-ends) to make all things accessible easily,
and manage the chaining of these things.
For instance, it should be possible to generate Go bindings in one command such as:

```sh
fidl gen --library my_library.fidl --binding go --out-dir go/src/my/library
```

Or format a library in place with:

```sh
fidl fmt --library my_library.fidl -i
```

<!-- xrefs -->
[be-c]: /zircon/system/host/fidl/lib/c_generator.cpp
[be-cpp]: /garnet/go/src/fidl/compiler/backend/cpp/
[be-dart]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidlgen_dart/
[be-go]: /garnet/go/src/fidl/compiler/backend/golang/
[be-rust]: /garnet/go/src/fidl/compiler/backend/rust/
[be-js]: https://chromium.googlesource.com/chromium/src/+/master/build/fuchsia/fidlgen_js/
[bindings_test-dart]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidl_bindings_test
[compatibility_test]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidl_compatibility_test
[fidlc-source]: /zircon/system/host/fidl/
[fidlc-tests]: /zircon//system/utest/fidl/
[jsonir]: /docs/development/languages/fidl/reference/json-ir.md
[rtl-c]: /zircon/system/ulib/fidl/
[rtl-cpp]: /garnet/lib/fidl/cpp/
[rtl-dart]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fidl/
[rtl-go]: /third_party/go/src/syscall/zx/fidl/
[rtl-rust]: /garnet/public/lib/fidl/rust/fidl/
[rtl-js]: https://chromium.googlesource.com/chromium/src/+/master/build/fuchsia/fidlgen_js/runtime/
[getting_started]: /docs/getting_started.md
[go-test-flags]: https://golang.org/cmd/go/#hdr-Testing_flags
