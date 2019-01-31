# zx_pmt_unpin

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

pmt_unpin - unpin pages and revoke device access to them

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_pmt_unpin(zx_handle_t handle);
```

## DESCRIPTION

`zx_pmt_unpin()` unpins pages that were previously pinned by [`zx_bti_pin()`],
and revokes the access that was granted by the pin call.

Always consumes *handle*. It is invalid to use *handle* afterwards, including
to call [`zx_handle_close()`] on it.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

On success, `zx_pmt_unpin()` returns **ZX_OK**.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a PMT handle.

## SEE ALSO

 - [`zx_bti_create()`]
 - [`zx_bti_pin()`]
 - [`zx_bti_release_quarantine()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_bti_create()`]: bti_create.md
[`zx_bti_pin()`]: bti_pin.md
[`zx_bti_release_quarantine()`]: bti_release_quarantine.md
[`zx_handle_close()`]: handle_close.md
