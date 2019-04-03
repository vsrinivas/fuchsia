
# Internal library zx

The `fidlc` compiler automatically generates `library zx` (internally) into
[//zircon/system/host/fidl/lib/library_zx.cpp](/zircon/system/host/fidl/lib/library_zx.cpp).

You will find content similar to the following:

```fidl
[Internal]
library zx;
using status = int32;
using time = int64;
using duration = int64;
using koid = uint64;
using vaddr = uint64;
using paddr = uint64;
using paddr32 = uint32;
using gpaddr = uint64;
using off = uint64;
using procarg = uint32;
const uint64 CHANNEL_MAX_MSG_BYTES = 65536;
const uint64 CHANNEL_MAX_MSG_HANDLES = 64;
```

You can reference this library with the `using` statement:

```fidl
using zx;
```

The types generally correspond to [Zircon System
Types](/docs/development/api/system.md#types). For example,
`zx.duration` corresponds to `zx_duration_t`.

> The `CHANNEL_MAX_MSG_BYTES` and `CHANNEL_MAX_MSG_HANDLES`
> are bound at `fidlc` compile time (that is, when the **compiler**
> is compiled) and reflect the constants present at that time.

