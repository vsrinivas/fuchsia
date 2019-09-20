# GIDL

GIDL is a code generation tool to create Golden FIDL Tests. It generates code
that tests whether FIDL bindings correctly encode and decode FIDL to/from the
wire format. Standard `.fidl` files define the FIDL data structures, and `.gidl`
files use those data structures and define the expected bytes for the
wire-format. The _gidl_ tool reads theses `.gidl` files, and outputs
_conformance tests_ that verifies the FIDL definitions match their expected
wire-format bytes.

GIDL supports multiple languages, and will generate a conformance test for each
supported language.

## Using GIDL

The input files for GIDL are at <//tools/fidl/gidl-conformance-suite/>. That
directory contains a `.gidl` file and multiple `.fidl` files.

Before building and running tests, be sure that you have the [configured the
build][fx set] to include the `tests` bundle. For example, if using the `core`
product configuration and `x64` architecture:

    fx set core.x64 --with //bundles:tests

To re-generate all of GIDL's conformance tests:

    fx build tools/gidl
    fx exec $FUCHSIA_DIR/tools/fidl/gidl-conformance-suite/regen.sh

After generating the conformance tests, you must run them to validate that the
conformance test passes. This is dependent on the language:

* Go:
    * Bindings: <third_party/go/src/syscall/zx/fidl/conformance/impl.go>
    * Test: <third_party/go/src/syscall/zx/fidl/fidl_test/conformance_test.go>
    * Build: `fx build third_party/go:go_fidl_tests`
    * Run: `fx run-test go_fidl_tests -- -test.v`

* C++ (HLCPP):
    * Bindings: <sdk/lib/fidl/cpp/conformance.fidl.h>
    * Test: <sdk/lib/fidl/cpp/conformance_test.cc>
    * Build: `fx build sdk/lib/fidl/cpp:conformance_test`
    * Run: `fx run-test fidl_tests`
    * Build (host): `fx build host_x64/fidl_cpp_host_conformance_test`
    * Run (host): `fx run-host-tests fidl_cpp_host_conformance_test`

* C++ (LLCPP):
    * Bindings: <out/default/fidling/gen/tools/fidl/gidl-conformance-suite/conformance/llcpp/fidl.h>
    * Test: <garnet/public/lib/fidl/llcpp/conformance_test.cc>
    * Build: `fx build garnet/public/lib/fidl/llcpp:fidl_llcpp_conformance_test`
    * Run: `fx run-test fidl_llcpp_conformance_test`

* Dart:
    * Bindings: <bin/fidl_bindings_test/test/test/conformance_test_types.dart>
    * Test: <bin/fidl_bindings_test/test/test/conformance_test.dart>
    * Build: `fx build topaz/bin/fidl_bindings_test/test:fidl_bindings_test`
    * Run: `fx run-test fidl_bindings_test`

## Writing Conformance Tests

There are three kinds of tests which can be expressed. We describe them below.

### Success

A `success` test case captures a value (optionally with handles), and its wire
format representation.

Here is an example:

    // Assuming the following FIDL definition:
    //
    // struct OneStringOfMaxLengthFive {
    //     string:5 the_string;
    // };

    success("OneStringOfMaxLengthFive-empty") {
        value = OneStringOfMaxLengthFive {
            the_string: "",
        }
        bytes = {
            0, 0, 0, 0, 0, 0, 0, 0, // length
            255, 255, 255, 255, 255, 255, 255, 255, // alloc present
        }
    }

From this description, the following must be verified:

* Encoding of the value into bytes
* Decoding of the bytes into the value
* Round-trips from value to bytes, back to value, back to bytes

### Fails to Encode

_GIDL does not currently output conformance tests for `fails_to_encode` cases_.

A `fails_to_encode` test case captures a value (optionally with handles), which
fails to encode (e.g. constraints not met).

Here is an example:

    fails_to_encode("OneStringOfMaxLengthFive-too-long") {
        value = OneStringOfMaxLengthFive {
            the_string: "bonjour", // 6 characters
        }
        err = FIDL_STRING_TOO_LONG
    }

From this description, the following must be verified:

* Encoding of the value fails
* Failure triggers exactly the expected error

### Fails to Decode

_GIDL does not currently output conformance tests for `fails_to_decode` cases_.

A `fails_to_decode` test case captures bytes encoding (optionally with handles),
which fails to decode (e.g. incorrect wire encoding).

Here is an example:

    fails_to_decode("OneStringOfMaxLengthFive-wrong-length") {
        bytes = {
            1, 0, 0, 0, 0, 0, 0, 0, // length
            255, 255, 255, 255, 255, 255, 255, 255, // alloc present
            // one character missing
        }
        err = FIDL_STRING_INCORRECT_SIZE
    }

From this description, the following must be verified:

* Decoding of the bytes encoding fails
* Failure triggers exactly the expected error

[fx set]: https://fuchsia.dev/fuchsia-src/development/workflows/fx#configure-a-build
