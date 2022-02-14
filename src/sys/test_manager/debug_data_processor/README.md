# debug_data_processor

Reviewed on: 2022-02-14

*debug_data_processor* is a small component invoked by the Rust
[*debug_data*][debug-data] component that handles the actual processing of debug data. It
exists because the debug data processing libraries are not directly accessible
from Rust.

## Design

The *debug_data_processor* component exposes the
[`fuchsia.test.debug.DebugDataProcessor`][internal-fidl] protocol. The
protocol allows saving debug data to a directory specified by the client, and
provides a signal when debug data is complete.

## Testing

Unit tests for debug_data_processor components can be run with

```
$ fx test debug_data_processor_unittests
```

## Source layout

The entry point is in `processor_main.cc`.

[debug-data]: /src/sys/test_manager/debug_data
[debug-fidl]: /src/sys/test_manager/fidl/fuchsia.test.debug

