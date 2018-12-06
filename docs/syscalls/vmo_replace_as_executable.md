# zx_vmo_replace_as_executable

## NAME

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

vmo_replace_as_executable - add execute rights to a vmo

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_replace_as_executable(zx_handle_t vmo, zx_handle_t vmex, zx_handle_t* out);

```

## DESCRIPTION

**vmo_replace_as_executable**() creates a replacement for *vmo*, referring
to the same underlying VM object, adding the right **ZX_RIGHT_EXECUTE**.

*vmo* is always invalidated.

*vmex* may currently be **ZX_HANDLE_INVALID** to ease migration of new code,
this is TODO(SEC-42) and will be removed.

## RIGHTS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

*handle* must be of type **ZX_OBJ_TYPE_VMO**.

*vmex* must have resource kind **ZX_RSRC_KIND_VMEX**.

## RETURN VALUE

**vmo_replace_as_executable**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *vmo* isn't a valid VM object handle, or
*vmex* isn't a valid **ZX_RSRC_KIND_VMEX** resource handle.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[resource_create](resource_create.md),
[vmar_map](vmar_map.md).
