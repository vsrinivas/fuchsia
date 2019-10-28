## FIDL definitions used in unit testing

The FIDL workflow is tested at multiple levels. `fidl_coded_types.cc` contains hand-written
coding tables for the message types, and their corresponding C structure definitions are found in
`fidl_structs.h`. Most tests in encoding/decoding exercise these manual coding table definitions.
Though not one-to-one generated, `messages.test.fidl` contains a general outline of the FIDL
definitions under test, for reference.

On the other hand, certain FIDL constructs are used in the higher layers, but are not supported
by the C bindings right now, e.g. tables. `fidlc` is able to generate the coding tables for FIDL
tables, but cannot generate their binding APIs. In order to unit test the table code paths, we will
generate and check in their coding tables `extra_messages.cc` from `extra_messages.test.fidl`.

The command to generate the contents of generated is:

```bash
fx build
fx exec ./gen.sh
```

The manual generation/checking-in should go away once we have a more flexible build process that
allows a test to declare dependency only on the coding tables, not the C client/server bindings.
Alternatively we could add tables support to C/low-level C++ bindings (FIDL-431).
