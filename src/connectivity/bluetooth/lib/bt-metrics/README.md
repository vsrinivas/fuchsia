# Bluetooth Lib: Metrics Logging

This library is meant to provide helper functions for logging in Bluetooth code.

## Cobalt logging

Currently, all the helper functions in this library are utility functions for
initializing and using Cobalt logger for Bluetooth specific metrics.

Any component that uses `"bt-metrics-logging` library should have access to
the `MetricEventLoggerFactory` capability. For example:

```
// Component manifest for a component that uses this library.
{
    ...
    use: [
        ...
        {
            protocol: [ "fuchsia.metrics.MetricEventLoggerFactory" ],
        },
    ],
    ...
}
```
