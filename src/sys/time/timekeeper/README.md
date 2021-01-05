# Timekeeper

## Overview

Timekeeper is responsible for starting and maintaining the UTC clock. For more
information on the design of UTC on Fuchsia please
refer to [UTC architecture](/docs/concepts/time/utc/architecture.md). To
understand the observable behavior of UTC on Fuchsia please refer to
[UTC behavior](/docs/concepts/time/utc/behavior.md)

Timekeeper connects to the `fuchsia.time.Maintenance` service to acquire a
writable handle to the UTC clock on launch. It launches and connects to a time
source component (or components) and connects to
`fuchsia.time.external.PushSource` to receive time synchronization information.

Timekeeper also connects to the Real Time Clock (RTC) driver using
`fuchsia.hardware.rtc` if one is found in `/dev/class/rtc`, reading the RTC at
initialization and updating it each time a new time sample is received from a
time source.

The algorithms used in Timekeeper are documented and explained at
[UTC algorithms](/docs/concepts/time/utc/algorithms.md).


## Design

Timekeeper is composed of the following Rust modules:

Module | Description
-------|------------
`main` | Entry point for the component that handles initialization and delegation to the other modules.
`enums` | A collection of simple enumerations that are used across the other modules and in particular are used to bridge the operational and diagnostics modules.
`util` | A collection of utility methods that are used across the other modules.
`rtc` | Abstracts discovery of and interaction with the real time clock driver away from other modules.
`time_source` | Abstracts the launching and interaction with time sources away from other modules. Produces a stream of time source events.
`time_source_manager` | Maintains a functional connection to a time source by using `time_source` to launch and relaunch the component as needed. `time_source_manager` validates the time samples received from `time_source` and produces a stream of validated time samples.
`estimator` | Abstracts the details of the Kalman filter and frequency correction algorithms away from other modules. `estimator` maintains a best estimate of the current UTC and frequency given a sequence of valid time samples.
`clock_manager` | Maintains a kernel clock object using `time_source_manager` to supply valid time samples and `estimator` to maintain the best estimate of UTC given these samples. `clock_manager` determines the most appropriate clock updates to converge the reported time with the estimated time.
`diagnostics` | Provides a common trait to supply timekeeping events of note to diagnostics systems and implements this trait for Cobalt and Inspect.


## Development and testing

Because timekeeper is implemented in Rust, we recommend that you consult the
[Fuchsia Rust documentation](/docs/development/languages/rust/README.md).

`timekeeper` itself is included in the `core` product configuration, no specific
`fx set` is needed to ensure it is included in an image.

Timekeeper is covered by both unit tests and integration tests. These are not
included by default so should be added to the available packages using `fx set`
or `fx args`:

* Unit tests are at `//src/sys/time/timekeeper:tests` and may be executed with
  `fx test timekeeper-tests`.
* Integration tests are at `//src/sys/time/timekeeper_integration` and may be
  executed with `fx test timekeeper-integration`