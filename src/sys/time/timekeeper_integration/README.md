# Timekeeper integration tests

Tests Timekeeper interactions against fake time sources and fake RTC devices.
During an integration test, Timekeeper launches and connects to
`dev_time_source`, a fake time source that forwards connections to the
integration test.

The test component implements a number of services:
 * `fuchsia.time.Maintenance` - provides timekeeper with a handle to a clock
 created by the test component.
 * `test.time.TimeSourceControl` - allows a `dev_time_source` launched
 by Timekeeper to forward the `fuchsia.time.external.*` connections it recieves from
 Timekeeper to the test component.
 * `fuchsia.net.interfaces.State` - a fake that accepts connections, but immediately
 closes the channel. This fake exists to allow timekeeper to bypass the network check
 and will be removed once time sources check for network availability instead.
