# Integration tests

This directory contains tests for functionality that spans more than one
component or area. Usually these tests are not unittests, but rather integration
tests.

## End to end product tests

Specifically, end to end (E2E) product tests for fuchsia products are located
here.

Unlike other kinds of tests, end to end product tests are specific to a product,
and therefore must not be included in the global `tests` build target mandated
by the [source tree layout](../../docs/development/source_code/layout.md).
Instead, end to end product tests are included directly in the build
configuration of their products in [//products](../../products/).
