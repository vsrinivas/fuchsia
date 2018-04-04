# zx_pmt_unpin

## NAME

pmt_unpin - unpin pages and revoke device access to them

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_pmt_unpin(zx_handle_t pmt);
```

## DESCRIPTION

**pmt_unpin**() unpins pages that were previously pinned by **bti_pin**(),
and revokes the access that was granted by the pin call.

On success, this syscall consumes the handle *pmt*.  It is invalid to use
*pmt* afterwards, including to call **handle_close**() on it.

## RETURN VALUE

On success, **pmt_unpin**() returns *ZX_OK*.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *pmt* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *pmt* is not a PMT handle.

## SEE ALSO

[bti_create](bti_create.md),
[bti_release_quarantine](bti_release_quarantine.md),
[bti_pin](bti_pin.md).
