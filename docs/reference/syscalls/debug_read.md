# zx_debug_read

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

TODO(fxbug.dev/32938)

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_debug_read(zx_handle_t handle,
                          char* buffer,
                          size_t buffer_size,
                          size_t* actual);
```

## DESCRIPTION

TODO(fxbug.dev/32938)

To use the `zx_debug_read()` function, you must specify
`kernel.enable-debugging-syscalls=true` on the kernel command line. Otherwise,
the function returns **ZX_ERR_NOT_SUPPORTED**.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

*handle* must have resource kind **ZX_RSRC_KIND_ROOT**.

## RETURN VALUE

TODO(fxbug.dev/32938)

## ERRORS

TODO(fxbug.dev/32938)

**ZX_ERR_NOT_SUPPORTED**  `kernel.enable-debugging-syscalls` is not set to `true`
on the kernel command line.

## SEE ALSO


TODO(fxbug.dev/32938)
