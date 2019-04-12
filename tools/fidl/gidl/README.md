# GIDL

GIDL is a code generation tool to create Golden FIDL Tests. The focus is encoding
and decoding tests, to ensure that various bindings properly follow the
wire format specification.

## Using GIDL

Regenerate conformance tests:

    fx full-build && ./tools/fidl/gidl-conformance-suite/regen.sh

## Writing Conformance Tests

There are three kinds of tests which can be expressed. We describe them below.

### Success

A `success` test case captures a value (optionally with handles), and its wire
format representation.

Here is an example:

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
* Rountrips from value to bytes, back to value, back to bytes

### Fails to Encode

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

A `fails_to_decode` test case captures bytes encoding (optionally with handles),
which fails to decode (e.g. incorrect wire encoding).

Here is an example:

    fails_to_decode("OneStringOfMaxLengthFive-wrong-lenght") {
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
