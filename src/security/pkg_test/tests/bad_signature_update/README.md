# Security Package Delivery Test Case: OTA Bad Signature

When a Fuchsia system downloads a software update the software delivery system
is responsible for checking the signature of downloaded packages. This test
simulates an update that contains a bad signature.

## Test Composition

Detailed component composition is articulated in `.cml` files in the `meta/`
subdirectory. In general, the test brings up the `hello_world_v0` system
assembly in an isolated `fshost` and initiates an update to `hello_world_v1` via
an isolated `netstack` and `pkg_server`.
