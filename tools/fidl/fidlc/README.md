# fidlc

The fidlc code lives in //zircon/tools/fidl. Once build unification is complete,
it can be moved here. In the meantime, this directory just contains the goldens.

## Goldens

The fidlc goldens are the result of running fidlc on the test libraries in
//zircon/tools/fidl/testdata (see README.md there). In particular, we store
golden files for the JSON IR and coding tables.

To regenerate, run `fx regen-goldens fidlc`.

To test, run `fx test fidlc_golden_tests`.
