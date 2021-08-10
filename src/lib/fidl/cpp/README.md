# Unified C++ bindings headers

The headers in this directory are meant to support the unified C++ bindings
(fxbug.dev/60240). The headers would be exposed to users via the same include
pattern as those from the high-level C++ bindings at //sdk/lib/fidl/cpp,
i.e. `#include <lib/fidl/cpp/foobar.h>`. The intention is that as the unified
C++ bindings becomes the prevalent C++ bindings API, we won't have to make
another LSC to move the header locations. Therefore, a bit of extra attention is
needed to ensure the headers and definitions in this library don't collide with
those in //sdk/lib/fidl/cpp.

Different from //sdk/lib/fidl/cpp, this library has an explicit "include" folder
that hosts the headers. This is desirable because we avoid exposing the entire
"//src" tree available for inclusion otherwise. Given enough time, we should
migrate libraries in //sdk to use a dedicated "include" folder too.
