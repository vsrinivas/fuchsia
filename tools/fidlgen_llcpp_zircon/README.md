## Generated FIDL Low-Level C++ Bindings

Because of BLD-427 and BLD-353 blocking invoking `fidlgen` from the zircon build,
we intend to check in copies of llcpp bindings for select FIDL libraries,
as a workaround, to support limited use of llcpp.

Each checked in library can be referenced similar to the C bindings, e.g. whereas one would write
`"$zx/system/fidl/fuchsia-mem:c"` to get the auto-generated C FIDL bindings, the llcpp version is
`"$zx/system/fidl/fuchsia-mem:llcpp"`.

When using it in source code, whereas one would write `#include <fuchsia/mem/c/fidl.h>` to import
the C bindings header, the corresponding llcpp directive would be
`#include <fuchsia/mem/llcpp/fidl.h>`.

To regenerate all the bindings, simply run the following command:
```bash
fx build -k 0 tools/fidlgen_llcpp_zircon:update
```
The `-k 0` switches would keep the build going even if parts of zircon failed to build.

As an extra precaution measure, the full build will validate that the generated bindings are
up to date. You can manually run the same check with the following command:
```bash
fx build tools/fidlgen_llcpp_zircon:validate
```
