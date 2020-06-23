## FIDL definitions used in unit testing

The FIDL workflow is tested at multiple levels. `fidl_coded_types.cc` contains hand-written
coding tables for the message types, and their corresponding C structure definitions are found in
`fidl_structs.h`. Most tests in encoding/decoding exercise these manual coding table definitions.
Though not one-to-one generated, `messages.test.fidl` contains a general outline of the FIDL
definitions under test, for reference.

On the other hand, certain FIDL constructs are used in the higher layers, but are not supported
by the C bindings right now, e.g. tables. `fidlc` is able to generate the coding tables for FIDL
tables, but cannot generate their binding APIs. In order to unit test the table code paths, we will
generate and check in their LLCPP bindings `extra_messages.h` from `extra_messages.test.fidl`.

