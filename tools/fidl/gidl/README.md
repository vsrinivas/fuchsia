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

## Structure

* `parser`: Code for parsing GIDL syntax into the IR (see next item).
* `ir`: The in memory representation of a suite of GIDL tests. For example, it
  defines types to represent each type of test: success, encode/decode_success/failure.
* `mixer`: Provides a `Schema` that wraps the FIDL IR. The mixer is responsible
  for validating that FIDL value in GIDL match their corresponding type
  declaration
* Backends (`cpp`, `dart`, etc.): Each backend takes in GIDL IR and FIDL IR,
  validates it using the mixer, and outputs the target language specific tests.

## Using GIDL

The input files for GIDL are at <//src/tests/fidl/conformance_suite/>. That
directory contains multiple `.gidl` and `.fidl` files.

Testing gidl:

    fx test //tools/fidl/gidl

Testing gidl and all conformance tests:

    fidldev test gidl

Refer to the FIDL [contributing doc][contributing] for how to set up `fidldev`.

To run conformance tests in a specific binding, you can use `--dry-run` to print
out the test command for all of the conformance tests, then pick out the ones
you want to run:

    fidldev test gidl --dry-run

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

[fx set]: https://fuchsia.dev/fuchsia-src/development/workflows/fx#configure-a-build
[contributing]: /docs/contribute/contributing-to-fidl
