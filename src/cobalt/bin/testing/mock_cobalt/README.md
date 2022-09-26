# Mock Cobalt Service

This mock cobalt service is intended for uses when you want to validate that code under test is
connecting to the cobalt service with the expected parameters and logging expected events.

It implements the `fuchsia.metrics.MetricEventLoggerFactory` and `fuchsia.metrics.MetricEventLogger`
fidl protocols and provides an additional `fuchsia.metrics.test.MetricEventLoggerQuerier` fidl
protocol for querying and resetting the state of the mock service.

mock_cobalt is intended to be used as described in [Test Component](https://fuchsia.dev/fuchsia-src/development/testing/test_component)
with the `fuchsia.metrics.MetricEventLoggerFactory` used as an injected service. Tests can then
connect to the `MetricEventLoggerQuerier` to assert that the expected events were logged.

The mock _does not_ perform any validation of metrics sent against a cobalt registry. It is up to
the test to check that expected event types and values are being sent by the code under test.
