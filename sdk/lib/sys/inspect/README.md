# sys\_inspect library

This document was last reviewed for accuracy on: 2019-07-16

This library supports creating a default Inspector for a Fuchsia Component.

Full documentation for the Component Inspection project can be found
[here](https://fuchsia.dev/fuchsia-src/development/inspect).

## Using

sys\_inspect can be used in-tree by depending on the `//sdk/lib/sys/inspect/cpp`
GN target and using `#include <lib/sys/inspect/cpp/component.h>`.

Our quickstart guide is available
[here](https://fuchsia.dev/fuchsia-src/development/inspect/quickstart.md).

Inspect data may be read from components using the 
[iquery](https://fuchsia.dev/fuchsia-src/development/inspect/iquery.md)
tool.

TODO(fxbug.dev/4548): sys\_inspect will be available in the SDK.

## Testing

Unit tests for inspect are available in the `sys_inspect_cpp_tests` package.

To include them, you must pass `--with //sdk/lib/sys/inspect/cpp/tests:sys_inspect_cpp_tests`
to `fx set`.

```
$ fx run-test sys_inspect_cpp_tests
```
