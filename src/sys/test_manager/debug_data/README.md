# debug_data

Reviewed on: 2022-02-14

The debug_data components collect debug data output by test components
via the [`fuchsia.debugdata.DebugData`][debug-fidl] protocols. This protocol
is most commonly used in tests to output profiling information that is used
to produce coverage data. Currently, debug_data components also merge together
profiles produced for reporting coverage, which reduces the size of profiles
pulled off device.

The debug_data components are delivered in the same package as `test_manager`,
and should be considered an implementation detail of `test_manager`.

Currently, there are two debug_data components:
 * *debug_data* - the component that will replace the C++
 debug_data component. It implements the
 [`fuchsia.test.internal.DebugDataController`][internal-fidl] protocol,
 which allows the caller to specify which realms to collect debug data for,
 and provides a synchronization signal that indicates when processing is
 complete.
 * [*C++ debug_data_processor*][debug-data-processor] - a small component
 invoked by the Rust debug_data component that actually processes the debug
 data. It exists because the debug data processing libraries are not directly
 accessible from Rust.

## Design

The *debug_data* component exposes the
[`fuchsia.test.internal.DebugDataController`][internal-fidl] protocol, which
adds the concept of a Set. A Set is a group of test realms for which
to collect debug data. Sets provide isolation. For example, when two test runs
are occurring at the same time, collecting debug data in two separate sets
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

Unit tests can be run with

```
$ fx test debug-data-rust-unittests
```

## Source layout

The entry point for *debug_data* is in `src/main.rs`.

## Future Work

 * Either modularize how debug data is processed and merged, or remove
 merging altogether. This would support "pluggable" processing for different
 types of debug data.

[debug-fidl]: /sdk/fidl/fuchsia.debugdata
[internal-fidl]: /sdk/fidl/fuchsia.test.internal
[debug-data-processor]: /src/sys/test_manager/debug_data_processor
