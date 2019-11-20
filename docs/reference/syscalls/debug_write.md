# zx_debug_write

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

TODO(fxbug.dev/32938)

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_debug_write(const char* buffer, size_t buffer_size);
```

## DESCRIPTION

TODO(fxbug.dev/32938)

To use the `zx_debug_write()` function, you must specify
`kernel.enable-debugging-syscalls=true` on the kernel command line. Otherwise,
the function returns **ZX_ERR_NOT_SUPPORTED**.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

TODO(fxbug.dev/32938)

## ERRORS

TODO(fxbug.dev/32938)

**ZX_ERR_NOT_SUPPORTED**  `kernel.enable-debugging-syscalls` is not set to `true`
on the kernel command line.

## SEE ALSO


TODO(fxbug.dev/32938)
