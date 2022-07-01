# FIDL Server Test Suite

The server test suite is a framework to test the implementation of servers in
FIDL bindings, such as how they respond to incorrect ordinals, an unexpected
channel closure, and other protocol level semantic aspects.

Each test involves:
- A test harness, which executes the client-side of the test and makes
assertions.
- A server - implemented in the binding under test, which implements a
prescribed set of behaviors.

This test suite uses three main FIDL protocols:
- Runner - coordinates running the test, starting the test server etc.
- Target - the protocol implemented by the server under test.
- Reporter - reports actions to the test harness.

These protocol definitions can be found in the
[FIDL server suite](/src/test/fidl/server_suite/fidl/serversuite.test.fidl).

The various test cases which leverage the framework are in
[harness/tests.cc](src/tests/fidl/server_suite/harness/tests.cc).

## Running the tests

To run the server test suite

    fx set core.x64 --with //bundles/fidl:tests

Then

    fx test fidl-server-suite-rust-test   (e.g. for rust)

To run all bindings use

   fx test //src/tests/fidl/server_suite
