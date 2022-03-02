# C++ Driver for Structured Config Client Library Integration Test

The structured config client library integration test provides each client
with a structured configuration and attempts to read it back using:

* the ConfigReceiverPuppet protocol exposed by the component
* the Inspect `config` node of the component

For the driver client, we use a static `shim` component written in Rust. This component
launches the driver using RealmBuilder.

Since the driver is launched using RealmBuilder, we can't use static routing to
expose `ConfigReceiverPuppet` to the test. Instead, the shim offers the driver the
`ConfigShim` protocol. The driver reports the configuration to the shim which forwards
it back to the test.

The test uses a different selector to read the Inspect `config` node of the driver.
