# memfs: a simple in-memory filesystem

This library implements a simple in-memory filesystem for Fuchsia.

It currently has no settings. Because it uses the C allocator, it can expand to
fill all available memory and the max size values from POSIX `statvfs()` and FIDL
`fuchsia.io.Node.QueryFilesystem()` may not have much meaning.

## Usage

Typical in-tree C++ users should depend on `//src/storage/memfs:cpp` and
`#include "src/storage/memfs/scoped_memfs.h"`.

```cpp
// Use a separate message loop to avoid deadlock on shutdown. The loop should
// enclose the memfs lifetime.
async::Loop loop(kAsyncLoopConfigNoAttachToCurrentThread);
loop.StartThread();

zx::result<ScopedMemfs> memfs =
    ScopedMemfs::CreateMountedAt(loop.dispatcher(), "/my_tmp");
```

Out-of-tree and C users should depend on `//src/storage/memfs` and `#include <lib/memfs/memfs.h>`.

