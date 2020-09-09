# Inspect Library

This document was last reviewed for accuracy on: 2019-10-31

Inspect is a library for exposing structured, hierarchical
diagnostics information from components at runtime.  Full
documentation for the Component Inspection project can be found
[here](https://fuchsia.dev/fuchsia-src/development/inspect).

## Using

Inspect can be used in-tree by depending on the `//zircon/public/lib/inspect`
GN target and using `#include <lib/inspect/cpp/inspect.h>`.

Our quickstart guide is available
[here](https://fuchsia.dev/fuchsia-src/development/inspect/quickstart.md).

Inspect data may be read from components using the 
[iquery](https://fuchsia.dev/fuchsia-src/development/inspect/iquery.md)
tool.

Inspect is also available in the SDK.

### Includes

#### `#include <lib/inspect/cpp/inspect.h>`

Support for writing Inspect data to a VMO.

#### `#include <lib/inspect/cpp/reader.h>`

Support for reading Inspect data from a VMO or an `inspect::Snapshot`
of a VMO.

#### `#include <lib/inspect/cpp/health.h>`

Support for attaching health information to Inspect nodes. This
information can be read and aggregated by `iquery --health`.


## Testing

```
fx set ... --with //zircon/system/ulib/inspect:tests
fx test -od inspect-unittest-package
```
