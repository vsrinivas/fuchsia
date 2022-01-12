# debug_data

Reviewed on: 2022-01-11

The debug_data components collect debug data output by test components
via the [`fuchsia.debugdata.DebugData`][debug-fidl] protocols. This protocol
is most commonly used in tests to output profiling information that is used
to produce coverage data. Currently, debug_data components also merge together
profiles produced for reporting coverage, which reduces the size of profiles
pulled off device.

The debug_data components are delivered in the same package as `test_manager`,
and should be considered an implementation detail of `test_manager`.

Currently, there are three debug_data components:
 * *C++ debug_data* - the currently used component. As it lacks
 synchronization mechanisms that indicate when processing is complete, it
 is deprecated and will soon be removed.
 * *Rust debug_data* - the component that will replace the C++
 debug_data component. It implements the
 [`fuchsia.test.internal.DebugDataController`][internal-fidl] protocol,
 which allows the caller to specify which realms to collect debug data for,
 and provides a synchronization signal that indicates when processing is
 complete.
 * *C++ debug_data_processor* - a small component invoked by the Rust
 debug_data component that actually processes the debug data. It exists
 because the debug data processing libraries are not directly accessible from
 Rust.

## Design

As the *C++ debug_data* component will soon be removed, this section focuses on
the design of the *Rust debug_data* component.

The *Rust debug_data* component exposes the
[`fuchsia.test.internal.DebugDataController`][internal-fidl] protocol, which
adds the concept of a Set. A Set is a group of test realms for which
to collect debug data. Sets provide isolation. For example, when two test runs
are ocurring at the same time, collecting debug data in two separate sets
ensures that each run will see isolated results.

`test_manager` creates a new Set for each test run, and reports the realms that
will run within the test run to *debug_data*. *debug_data* in turn signals back
if any debug data was processed for the Set, and signals when all processing
for the Set is complete.

The *debug_data* component understands when it has completed processing all
debug data for a realm by inspecting a stream of component lifecycle events,
and determining when each realm has been destroyed (and therefore can produce
no further debug data).

## Testing

Unit tests for all the debug_data components can be run with

```
$ fx test debug-data-rust-unittests debug_data_unittests
```

## Source layout

The entrypoint for each component is as follows:
 * *C++ debug_data* component - `main.cc`
 * *Rust debug_data* component - `src/main.rs`
 * *C++ debug_data_processor* component - `processor_main.cc`

## Future Work

 * Remove the *C++ debug_data* component
 * Move the *debug_data_processor* component to a new directory
 * Complete implementation of the *Rust debug_data* component
 * Either modularize how debug data is processed and merged, or remove
 merging altogether. This would support "pluggable" processing for different
 types of debug data.

[debug-fidl]: /sdk/fidl/fuchsia.debugdata
[internal-fidl]: /sdk/fidl/fuchsia.test.internal

