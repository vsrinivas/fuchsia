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

The front-end lives at [//zircon/tools/fidl/][fidlc-source],
with tests in [//zircon/system/utest/fidl/][fidlc-tests].

The back-end and runtime library locations are based on the target:

Target     | Back-end                                               | Runtime Libraries
-----------|--------------------------------------------------------|------------------
C          | [//zircon/tools/fidl/lib/c_generator.cc][be-c]         | [//zircon/system/ulib/fidl/][rtl-c]
C++        | [//garnet/go/src/fidl/compiler/backend/cpp/][be-cpp]   | [//zircon/system/ulib/fidl/][rtl-c] & [//sdk/lib/fidl/cpp/][rtl-cpp]
Go         | [//garnet/go/src/fidl/compiler/backend/golang/][be-go] | [//third_party/go/src/syscall/zx/fidl/][rtl-go]
Rust       | [//garnet/go/src/fidl/compiler/backend/rust/][be-rust] | [//src/lib/fidl/rust/fidl/][rtl-rust]
Dart       | [//topaz/bin/fidlgen_dart/][be-dart]                   | [//topaz//public/dart/fidl/][rtl-dart]<br>[//topaz/bin/fidl_bindings_test/][bindings_test-dart]
JavaScript | [chromium:build/fuchsia/fidlgen_fs][be-js]             | [chromium:build/fuchsia/fidlgen_js/runtime][rtl-js]

### Other FIDL Tools

**TBD: linter, formatter, gidl, difl, regen scripts, etc.**

### Common Development Tools

This is a crowdsourced section from the FIDL team on useful tools that they
use for working on the FIDL codebase.

#### IDEs

Most of the FIDL team uses VSCode for development. Some useful plugins and workflows:

* The [remote ssh](https://code.visualstudio.com/docs/remote/ssh) feature works
really well for doing remote work from your laptop.
  * Setting up tmux or screen is also helpful for remote work, to preserve
  history and manage multiple sessions in the shell.
* The Fuchsia documentation has instructions for setting up language servers:
  * [clangd](/docs/development/languages/c-cpp/editors.md) for c++
  * [rls](/docs/development/languages/rust/editors.md) for rust
* The [rewrap extension](https://marketplace.visualstudio.com/items?itemName=stkb.rewrap) is useful
  for automatically reflowing lines to a certain length (e.g. when editing markdown files).
* To get automatic syntax highlighting for the bindings golden files, update the
  `file.associations` setting:

  ```json
  "files.associations": {
      "*.test.fidl.json.rs.golden": "rust",
      "*.test.fidl.json.cc.golden": "cpp",
      "*.test.fidl.json.h.golden": "cpp",
      "*.test.fidl.json.llcpp.cc.golden": "cpp",
      "*.test.fidl.json.llcpp.h.golden": "cpp",
      "*.test.fidl.json.h.go.golden": "go",
      "*.test.fidl.json_async.dart.golden": "dart",
      "*.test.fidl.json_test.dart.golden": "dart"
  },
  ```

### C++ Style Guide

We follow the [Fuchsia C++ Style Guide][cpp-style], with additional rules to
further remove ambiguity around the application or interpretation of guidelines.

#### Constructors

Always place the initializer list on a line below the constructor.

```cpp
// Don't do this.
SomeClass::SomeClass() : field_(1), another_field_(2) {}

// Correct.
SomeClass::SomeClass()
    : field_(1), another_field_(2) {}
```

#### Comments

Comments must respect 80 columns line size limit, unlike code which can extend
to 100 lines size limit.

##### Lambda captures

* If a lambda escapes the current scope, capture all variables explicitly.
* If the lambda is local (does not escape the current scope), prefer using a default capture by
  reference ("`[&]`").

Seeing `[&]` is a strong signal that the lambda exists within the current scope only, and can be
used to distinguish local from non-local lambdas.

```cpp
// Correct.
std::set<const flat::Library*, LibraryComparator> dependencies;
auto add_dependency = [&](const flat::Library* dep_library) {
  if (!dep_library->HasAttribute("Internal")) {
    dependencies.insert(dep_library);
  }
};
```

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

### symbolizer

To symbolize backtraces, you'll need a symbolizer in scope:

```sh
export ASAN_SYMBOLIZER_PATH="$FUCHSIA_DIR/prebuilt/third_party/clang/$HOST_PLATFORM/bin/llvm-symbolizer"
```

## Compiling, and Running Tests

We provide mostly one-liners to run tests for the various parts.
When in doubt, refer to the "`Test:`" comment in the git commit message;
we do our best to describe the commands used to validate our work there.

### fidlc

```sh
# optional; builds fidlc for the host with ASan <https://github.com/google/sanitizers/wiki/AddressSanitizer>
fx set core.x64 --variant=host_asan

# build fidlc
fx build zircon/tools
```

If you're doing extensive edit-compile-test cycles on `fidlc`, building with no optimizations can
make a significant difference in the build speed. To optimize the build, change the `opt_level`
setting in `zircon/public/gn/config/levels.gni`. But be careful: the kernel is not regularly
tested with `opt_level` below 2, and it does not support -1 at all (this setting can cause kernel
panics from stack overflows in the kernel).

To avoid accidentally committing this change, run:

```
git update-index --skip-worktree zircon/public/gn/config/levels.gni
```

If you want to allow the changes to be committed again, run:

```
git update-index --no-skip-worktree zircon/public/gn/config/levels.gni
```

### fidlc tests

fidlc tests are at:

* [//zircon/system/utest/fidl-compiler/][fidlc-compiler-tests].
* [//zircon/system/utest/fidl/][fidlc-tests].
* [//zircon/system/utest/fidl-coding/tables/][fidlc-coding-tables-tests].
* [//zircon/system/utest/fidl-simple][fidl-simple (C runtime tests)].

```sh
# build & run fidlc tests
fx build system/utest:host
$FUCHSIA_DIR/out/default.zircon/host-x64-linux-clang/obj/system/utest/fidl-compiler/fidl-compiler-test.debug

# build & run fidl-coding-tables tests
# --with-base puts all zircon tests under /boot with the bringup.x64 target, or /system when using the core.x64 target
fx set bringup.x64 --with-base //garnet/packages/tests:zircon   # optionally append "--variant asan"
fx build
fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-coding-tables-test
```

To regenerate the FIDL definitions used in unit testing, run:
```sh
fx build zircon/tools
$FUCHSIA_DIR/out/default.zircon/tools/fidlc \
  --tables $FUCHSIA_DIR/zircon/system/utest/fidl/fidl/extra_messages.cc \
  --files $FUCHSIA_DIR/zircon/system/utest/fidl/fidl/extra_messages.test.fidl
```

To regenerate the `fidlc` JSON goldens, ensure `fidlc` is built and up to date, then run:

```sh
fx exec $FUCHSIA_DIR/zircon/tools/fidl/testdata/regen.sh
```

These "golden" files are examples of what kind of JSON IR `fidlc` produces and
are used to track changes. It is required to regenerate the golden files each
time the JSON IR is changed in any way, otherwise the `json_generator_tests` fails.

### fidlgen (LLCPP, HLCPP, Rust, Go)

Build:

```sh
fx build garnet/go/src/fidl
```

Run:

```sh
$FUCHSIA_DIR/out/default/host_x64/fidlgen
```

Some example tests you can run:

```sh
fx run-host-tests fidlgen_cpp_test
fx run-host-tests fidlgen_golang_ir_test
```

To regenerate the goldens:

```sh
fx exec garnet/go/src/fidl/compiler/backend/typestest/regen.sh
```

### fidlgen_dart

Some example tests you can run:

```sh
fx run-host-tests fidlgen_dart_backend_ir_test
```

To regenerate the goldens:

```sh
fx exec topaz/bin/fidlgen_dart/regen.sh
```

### C runtime

```sh
fx set core.x64 --with-base //garnet/packages/tests:zircon
fx build
fx qemu -kN

# On Fuchsia device's shell
$ runtests -t fidl-test
$ runtests -t fidl-simple-test
```

The `-k` flag enables KVM. It is not required, but the emulator is *much* slower
without it. The `-N` flag enables networking.

You might get lucky with
```
fx qemu -kN -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-test
fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-simple-test
```

When the test completes, you're running in the QEMU emulator.
To exit, use **`Ctrl-A x`**.

Alternatively, if you build including the shell you can run these commands from
the shell:

```sh
Tab 1> fx set core.x64 --with-base //garnet/packages/tests:zircon
Tab 1> fx build && fx qemu -kN

Tab 2> fx shell
Tab 2(shell)> runtests -t fidl-simple-test
Tab 2(shell)> runtests -t fidl-test
```

Some of the C runtime tests can run on host:
```sh
fx build zircon && fx run-host-tests fidl-test
```
This only includes a few tests, so be sure to check the output to see if it is
running the test you care about.

### C++ runtime

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx qemu -kN

Tab 3> fx run-test fidl_tests
```

There are separate tests for LLCPP that can be run in the same way as `fidl_tests`:

* fidl_llcpp_types_test
* fidl_llcpp_conformance_test

### Go runtime

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx qemu -kN

Tab 3> fx run-test go_fidl_tests
Tab 3> fx run-test fidl_go_conformance
```

As with normal Go tests, you can pass [various flags][go-test-flags] to control
execution, filter test cases, run benchmarks, etc. For instance:

```sh
Tab 3> fx run-test go_fidl_tests -- -test.v -test.run 'TestAllSuccessCases/.*union.*'
```

### Rust runtime

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx qemu -kN

Tab 3> fx run-test rust_fidl_tests
```

### Dart runtime

The Dart FIDL bindings tests are in [//topaz/bin/fidl_bindings_test/][bindings_test-dart].

You first need to have Fuchsia running in an emulator. Here are the steps:

```sh
Tab 1> fx build && fx serve-updates

Tab 2> fx qemu -kN

Tab 3> fx run-test fidl_bindings_test
```

### Compatibility Test

Details about how the compatibility tests work and where the code is located can be
found in the README at [//garnet/bin/fidl_compatibility_test][compat_readme]

To run the compatibility tests, you first need to have Fuchsia running in an
emulator:

```sh
Tab 1> fx build && fx serve

Tab 2> fx qemu -kN

```

To run the compatibility tests with HLCPP, LLCPP, Rust, and Go:

```sh
Tab 3> fx set core.x64 --with-base //garnet/packages/tests:zircon --with //garnet/packages/tests:all
Tab 3> fx run-test fidl_compatibility_test
```

To run the compatiblity tests with Dart:

```sh
Tab 3> fx set core.x64 --with //topaz/packages/tests:all
Tab 3> fx run-test fidl_compatibility_test_topaz
```

### GIDL

To rebuild GIDL:

```sh
fx build host-tools/gidl
```

### All Tests

| Name                     | Test Command                                        | Directories Covered                                                     |
|--------------------------|-----------------------------------------------------|-------------------------------------------------------------------------|
| gidl parser              | fx run-host-tests gidl_parser_test                  | tools/fidl/gidl/parser                                                  |
| fidlgen hlcpp            | fx run-host-tests fidlgen_cpp_test                  | garnet/go/src/fidl/compiler/backend/cpp                                 |
| fidlgen llcpp            | fx run-host-tests fidlgen_llcpp_test                | garnet/go/src/fidl/compiler/llcpp_backend                               |
| fidlgen overnet          | fx run-host-tests fidlgen_cpp_overnet_internal_test | garnet/go/src/fidl/compiler/backend/cpp_overnet_internal                |
| fidlgen golang           | fx run-host-tests fidlgen_golang_test               | garnet/go/src/fidl/compiler/backend/golang                              |
| fidlgen golang ir        | fx run-host-tests fidlgen_golang_ir_test            | garnet/go/src/fidl/compiler/backend/golang/ir                           |
| fidlgen rust             | fx run-host-tests fidlgen_rust_test                 | garnet/go/src/fidl/compiler/backend/rust                                |
| fidlgen rust ir          | fx run-host-tests fidlgen_rust_ir_test              | garnet/go/src/fidl/compiler/backend/rust/ir                             |
| fidlgen syzkaller        | fx run-host-tests fidlgen_syzkaller_test            | garnet/go/src/fidl/compiler/backend/syzkaller                           |
| fidlgen syzkaller ir     | fx run-host-tests fidlgen_syzkaller_ir_test         | garnet/go/src/fidl/compiler/backend/syzkaller/ir                        |
| fidlgen type definitions | fx run-host-tests fidlgen_types_test                | garnet/go/src/fidl/compiler/backend/types                               |
| fidl c runtime host test | fx run-host-tests fidl-test                         | zircon/system/ulib/fidl                                                 |
| c++ host unittests       | fx run-host-tests fidl_cpp_host_unittests           | sdk/lib/fidl                                                            |
| c++ bindings tests       | fx run-test fidl_tests                              | sdk/lib/fidl                                                            |
| llcpp bindings tests     | fx run-test fidl_llcpp_types_test                   | garnet/go/src/fidl/compiler/llcpp_backend                               |
| go bindings tests        | fx run-test go_fidl_tests                           | third_party/go/syscall/zx/fidl third_party/go/syscall/zx/fidl/fidl_test |
| dart bindings tests      | fx run-test fidl_bindings_test                      | topaz/public/dart/fidl                                                  |
| rust bindings            | fx run-test rust_fidl_tests                         | src/lib/fidl/rust/fidl                                        |


The following requires: fx set bringup.x64 --with-base //garnet/packages/tests:zircon

| Name                      | Test Command                                                                                                  | Directories Covered     |
|---------------------------|---------------------------------------------------------------------------------------------------------------|-------------------------|
| fidlc host test           | $FUCHSIA_DIR/out/default.zircon/host-x64-linux-clang/obj/system/utest/fidl-compiler/fidl-compiler-test.debug  | zircon/system/host/fidl |
| fidl coding tables test   | fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-coding-tables-test                               | zircon/system/host/fidl |
| fidl c runtime test       | fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-test                                             | zircon/system/ulib/fidl |
| fidl c runtime test       | fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-simple-test                                      | zircon/system/ulib/fidl |
| fidl c-llcpp interop test | fx qemu -k -c zircon.autorun.boot=/boot/bin/runtests+-t+fidl-llcpp-interop-test                               | zircon/system/ulib/fidl |

### All Regen Commands

| Name                                 | Regen Commands                                                              | Input                                                             | Output                                                                                                                                                                                                                                                                                       |
|--------------------------------------|-----------------------------------------------------------------------------|-------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| fidlc goldens                        | fx exec $FUCHSIA_DIR/tools/fidl/testdata/regen.sh                           | zircon/tools/fidl/goldens                                         | zircon/tools/fidl/testdata                                                                                                                                                                                                                                                                   |
| fidlgen goldens                      | fx exec $FUCHSIA_DIR/garnet/go/src/fidl/compiler/backend/typestest/regen.sh | garnet/go/src/fidl/compiler/backend/goldens                       | garnet/go/src/fidl/compiler/backend/goldens                                                                                                                                                                                                                                                  |
| dart fidlgen goldens                 | fx exec $FUCHSIA_DIR/topaz/bin/fidlgen_dart/regen.sh                        | garnet/go/src/fidl/compiler/backend/goldens                       | topaz/bin/fidlgen_dart/goldens                                                                                                                                                                                                                                                               |
| dangerous identifiers                | garnet/tests/fidl-dangerous-identifiers/generate.py                         | garnet/tests/fidl-dangerous-identifiers/dangerous_identifiers.txt | garnet/tests/fidl-dangerous-identifiers/cpp/ garnet/tests/fidl-dangerous-identifiers/fidl/                                                                                                                                                                                                   |
| regen third party go                 | fx exec $FUCHSIA_DIR/third_party/go/regen-fidl                              |                                                                   |                                                                                                                                                                                                                                                                                              |
| regen c-llcpp interop test bindings  | fx exec $FUCHSIA_DIR/zircon/system/utest/fidl-llcpp-interop/gen_llcpp.sh    | zircon/system/utest/fidl-llcpp-interop/*.test.fidl                | zircon/system/utest/fidl-llcpp-interop/generated/                                                                                                                                                                                                                                            |
| regen llcpp fidl::Bind test bindings | fx exec $FUCHSIA_DIR/zircon/system/utest/fidl/gen.sh                        | zircon/system/utest/fidl/llcpp.test.fidl                          | zircon/system/utest/fidl/generated/                                                                                                                                                                                                                                                          |
| regen fidl-async test bindings       | fx exec $FUCHSIA_DIR/zircon/system/ulib/fidl-async/test/gen_llcpp.sh        | zircon/system/ulib/fidl-async/test/simple.test.fidl               | zircon/system/ulib/fidl-async/test/generated/                                                                                                                                                                                                                                                |
| regen llcpp service test bindings    | fx exec $FUCHSIA_DIR/zircon/system/utest/service/gen_llcpp.sh               | zircon/system/utest/service/test.test.fidl                        | zircon/system/utest/service/generated/                                                                                                                                                                                                                                                       |
| checked in production llcpp bindings | fx build -k 0 tools/fidlgen_llcpp_zircon:update                             | FIDL definitions in zircon/system/fidl/                           | "gen/" folder relative to the corresponding FIDL definition                                                                                                                                                                                                                                  |

## Debugging (host)

There are several ways of debugging issues in host binaries. This section gives
instructions for the example case where `fidlc --files test.fidl` is crashing:

- [GDB](#GDB)
- [Asan](#ASan)
- [Valgrind](#Valgrind)

Note: Even with all optimizations turned off, the binaries in
`out/default/host_x64` are stripped. For debugging, you should use the binaries
with the `.debug` suffix, such as
`out/default.zircon/host-x64-linux-clang/obj/tools/fidl/fidlc.debug`.

### GDB {#GDB}

Start GDB:

```sh
gdb --args out/default.zircon/host-x64-linux-clang/obj/tools/fidl/fidlc.debug --files test.fidl
```

Then, enter "r" to start the program.

### ASan {#ASan}

Ensure you are compiling with ASan enabled:

```sh
fx set core.x64 --variant=host_asan
fx build host_x64/fidlc
```

Then run `out/default/host_x64/fidlc --files test.fidl`. That binary should be
the same as `out/default.zircon/host-x64-linux-asan/obj/tools/fidl/fidlc`.

### Valgrind {#Valgrind}

On Google Linux machines, you may need to install a standard version of Valgrind
instead of using the pre-installed binary:

```
sudo apt-get install valgrind
```

Then:

```sh
valgrind -v -- out/default.zircon/host-x64-linux-clang/obj/tools/fidl/fidlc.debug --files test.fidl
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
[cpp-style]: /docs/development/languages/c-cpp/cpp-style.md
[be-c]: /zircon/tools/fidl/lib/c_generator.cc
[be-cpp]: /garnet/go/src/fidl/compiler/backend/cpp/
[be-dart]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidlgen_dart/
[be-go]: /garnet/go/src/fidl/compiler/backend/golang/
[be-rust]: /garnet/go/src/fidl/compiler/backend/rust/
[be-js]: https://chromium.googlesource.com/chromium/src/+/master/build/fuchsia/fidlgen_js/
[bindings_test-dart]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidl_bindings_test
[compatibility_test]: https://fuchsia.googlesource.com/topaz/+/master/bin/fidl_compatibility_test
[fidlc-source]: /zircon/tools/fidl/
[fidlc-coding-tables-tests]: /zircon/system/utest/fidl-coding-tables/
[fidl-simple]: /zircon/system/utest/fidl-simple/
[fidlc-compiler-tests]: /zircon/system/utest/fidl-compiler/
[fidlc-tests]: /zircon/system/utest/fidl/
[jsonir]: /docs/development/languages/fidl/reference/json-ir.md
[rtl-c]: /zircon/system/ulib/fidl/
[rtl-cpp]: /garnet/public/lib/fidl/llcpp/
[rtl-dart]: https://fuchsia.googlesource.com/topaz/+/master/public/dart/fidl/
[rtl-go]: https://fuchsia.googlesource.com/third_party/go/+/master/src/syscall/zx/fidl/
[rtl-rust]: /src/lib/fidl/rust/fidl/
[rtl-js]: https://chromium.googlesource.com/chromium/src/+/master/build/fuchsia/fidlgen_js/runtime/
[getting_started]: /docs/getting_started.md
[compat_readme]: /garnet/public/lib/fidl/compatibility_test/README.md
[go-test-flags]: https://golang.org/cmd/go/#hdr-Testing_flags
