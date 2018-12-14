# zx_system_mexec_payload_get

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

system_mexec_payload_get - Return a ZBI containing ZBI entries necessary to boot this system

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_system_mexec_payload_get(zx_handle_t resource,
                                        void* buffer,
                                        size_t buffer_size);
```

## DESCRIPTION

`zx_system_mexec_payload_get()` accepts a resource handle and a
pointer/length corresponding to an output buffer and fills the buffer with an
incomplete ZBI containing a sequence of entries that should be appended to a
ZBI before passing that image to [`zx_system_mexec()`].

*resource* must be of type **ZX_RSRC_KIND_ROOT**.

*buffer* and *buffer_size* must point to a buffer that is no longer than 16KiB.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*resource* must have resource kind **ZX_RSRC_KIND_ROOT**.

## RETURN VALUE

`zx_system_mexec_payload_get()` returns **ZX_OK** on success.

## SEE ALSO

 - [`zx_system_mexec()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_system_mexec()`]: system_mexec.md
