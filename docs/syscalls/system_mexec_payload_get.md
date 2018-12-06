# zx_system_mexec_payload_get

## NAME

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

system_mexec_payload_get - Return a ZBI containing ZBI entries necessary to boot this system

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_system_mexec_payload_get(zx_handle_t resource,
                                        void* buffer, size_t len);
```

## DESCRIPTION

**zx_system_mexec_payload_get**() accepts a resource handle and a
pointer/length corresponding to an output buffer and fills the buffer with an
incomplete ZBI containing a sequence of entries that should be appended to a
ZBI before passing that image to zx_system_mexec().

*resource* must be of type *ZX_RSRC_KIND_ROOT*.

*buffer* and *len* must point to a buffer that is no longer than 16KiB.

## RIGHTS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

*resource* must have resource kind **ZX_RSRC_KIND_ROOT**.

## RETURN VALUE

**zx_system_mexec_payload_get**() returns ZX_OK on success.

## SEE ALSO

[system_mexec](system_mexec.md).
