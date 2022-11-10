# Fake Boot Arguments for Security Package Delivery Tests

This component provides a fake `fuchsia.boot.Arguments` implementation for
security package delivery tests. For example, `pkg-cache` and `pkg-resolver`
both depend on this protocol to determine their configuration.

The component manifest is declared as an incomplete shard so that configuration
details can be determined by the underlying test.
