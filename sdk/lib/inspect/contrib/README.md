# Inspect Contributions

This document was last reviewed for accuracy on: 2019-08-20

The Inspect contrib libraries are contributions to the Inspect API that
are not yet included in the SDK artifacts for Inspect.

These libraries are available for use in-tree until they are promoted
to the SDK.

This library is **not** available in the SDK.

## Using

All contrib libraries may be included by depending on  `//sdk/lib/inspect/contrib/cpp`
GN target and adding the corresponding header to your C++ source file.

### ReadVisitor extension

`#include <lib/inspect/contrib/cpp/read_visitor.h>`

Includes functions to visit nodes of a read Inspect Hierarchy.

## Testing

To test all contributed libraries, run:

```
fx run-test inspect_contrib_cpp_tests
```
