# `dynfidl` Conformance Tests

This directory contains generated conformance tests styled after the GIDL test suite for FIDL.
The `test_from_ir` directory contains a host tool which parses the [JSON IR] from the FIDL toolchain
for a FIDL library to generate a "roundtrip test" for each type provided. Each roundtrip test
generates a domain value from the Rust FIDL bindings and a `dynfidl` builder for encoding a message.
The test encodes both into their persistent message encoding and ensures they are equal.

## Regenerating the tests

```sh
$ cd $FUCHSIA_DIR
$ fx set ... --with //src/lib/dynfidl:tests
$ ./src/lib/dynfidl/rust/tests/conformance/regenerate_lib_rs.sh
```

### Hazards

This script is currently hardcoded to use types from the GIDL conformance suite, the location or
names of which may change without updating the script.

The `test_from_ir` tool uses FIDL's [JSON IR] which may change without having updating the tool.

## Adding new instances of supported types to be tested

First, ensure that the type you want to add has an ignored test generated. If no empty test is being
generated, `test_from_ir` must be updated to preserve the declaration for the type. See the
`can_be_encoded_by_dynfidl()` methods for details.

Next, add the fully-qualified type name to the list in `./regenerage_lib_rs.sh` before generating
the tests.

## Adding other types to be tested

At the time of writing, `dynfidl` only supports encoding structs with `MaxHandles=0`. To emit
roundtrip tests for other FIDL types, add fields on the `FidlIr` type in `test_from_ir` and
define types for the portions of the metadata needed. The generator's main function will need to be
modified to support looking up types to test in a map of all declarations and to include the new
types in that map.

[JSON IR]: /docs/reference/fidl/language/json-ir.md
