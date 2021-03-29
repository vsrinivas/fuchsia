# Unified C++ FIDL bindings examples

This directory contains experimental example code for the nascent unified C++
bindings (fxbug.dev/60240). It follows the structure of `/examples/fidl`.
We can incubate the bindings API here.

To use the unified bindings for a library `//examples/fidl/fuchsia.examples`,
declare the following dependency:

```gn
deps = [
    "//examples/fidl/fuchsia.examples:fuchsia.examples_cpp",
]
```

and include the following header:

```cpp
#include <fuchsia/examples/cpp/fidl_v2.h>
```

After the unified bindings is sufficiently stable for general use,
we can move it to the top-level `/examples` directory.
