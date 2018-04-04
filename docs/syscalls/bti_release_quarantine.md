# zx_bti_release_quarantine

## NAME

bti_release_quarantine - releases all quarantined PMTs

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_bti_release_quarantine(zx_handle_t bti);

```

## DESCRIPTION

**bti_release_quarantine**() releases all quarantined PMTs for the given BTI.
This will release the PMTs' underlying references to VMOs and physical page
pins.  The underlying physical pages may be eligible to be reallocated
afterwards.

## RETURN VALUE

**bti_release_quarantine**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *bti* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *bti* is not a BTI handle.

**ZX_ERR_ACCESS_DENIED** *bti* does not have the *ZX_RIGHT_WRITE* right.

## SEE ALSO

[bti_pin](bti_pin.md),
[pmt_unpin](pmt_unpin.md).
