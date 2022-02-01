# FIDL Dynamic Test Suite

The dynamic test suite is a framework to test dynamic properties of FIDL
bindings, such as how they respond to incorrect ordinals, an unexpected channel
closure, and other protocol level semantic aspects.

You can read about how the architecture of the framework in [the
`fidl.dynsuite`](/src/tests/fidl/dyn_suite/fidl.dynsuite/dynsuite.test.fidl)
library.

The various test cases which leverage the framework are in
[harness/main.cc](src/tests/fidl/dyn_suite/harness/main.cc).

### Running the tests

To run client tests for the Go bindings

    fx set core.x64 --with //bundles/fidl:tests

Then

    fx test --test-filter='ClientTest.*' fidl-dyn-suite-go-test
    fx test --test-filter='ClientTest.*' fidl-dyn-suite-hlcpp-test
