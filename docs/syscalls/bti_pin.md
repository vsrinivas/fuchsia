# zx_bti_pin

## NAME

bti_pin - pin pages and grant devices access to them

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_bti_pin(zx_handle_t bti, uint32_t options,
                       zx_handle_t vmo, uint64_t offset, uint64_t size,
                       zx_paddr_t* addrs, size_t addrs_count,
                       zx_handle_t* pmt);
```

## DESCRIPTION

**bti_pin**() pins pages of a VMO (i.e. prevents them from being decommitted
with **[vmo_op_range](vmo_op_range.md)**()) and grants the hardware
transaction ID represented by the BTI the ability to access these pages,
with the permissions specified in *options*.

*offset* must be aligned to page boundaries.

*options* is a bitfield that may contain one or more of *ZX_BTI_PERM_READ*,
*ZX_BTI_PERM_WRITE*, *ZX_BTI_PERM_EXECUTE*, and *ZX_BTI_COMPRESS*.  In order
for the call to succeed, *vmo* must have the READ/WRITE/EXECUTE rights
corresponding to the permissions flags set in *options*.

If the range in *vmo* specified by *offset* and *size* contains non-committed
pages, a successful invocation of this function will result in those pages
having been committed.  On failure, it is undefined whether they have been
committed.

*addrs* will be populated with the device-physical addresses of the requested
VMO pages.  These addresses may be given to devices that issue memory
transactions with the hardware transaction ID associated with the BTI.  The
number of addresses returned depends on whether the *ZX_BTI_COMPRESS* option was
given.  It number of addresses will be either
1) If *ZX_BTI_COMPRESS* is not set, one per page (*size*/*PAGE_SIZE*)
2) If *ZX_BTI_COMPRESS* is set, *size*/*minimum-contiguity*, rounded up
   (each address representing the a run of *minimum-contiguity* run of bytes,
   with the last one being potentially short if *size* is not a multiple of
   *minimum-contiguity*).  It is guaranteed that all returned addresses will be
   *minimum-contiguity*-aligned.  Note that *minimum-contiguity* is discoverable
   via **[object_get_info](object_get_info.md)**().

*addrs_count* is the number of entries in the *addrs* array.  It is an error for
*addrs_count* to not match the value calculated above.

## OPTIONS

- *ZX_BTI_PERM_READ*, *ZX_BTI_PERM_WRITE*, and *ZX_BTI_PERM_EXECUTE* define the access types
that the hardware bus transaction initiator will be allowed to use.
- *ZX_BTI_COMPRESS* causes the returned address list to contain one entry per
  block of *minimum-contiguity* bytes, rather than one per *PAGE_SIZE*.

## RETURN VALUE

On success, **bti_pin**() returns *ZX_OK*.  The device-physical addresses of the
requested VMO pages will be written in *addrs*.  A handle to the created Pinned
Memory Token is returned via *pmt*.  When the PMT is no longer needed,
*pmt_unpin*() should be invoked.

In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *bti* or *vmo* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *bti* is not a BTI handle or *vmo* is not a VMO handle.

**ZX_ERR_ACCESS_DENIED** *bti* or *vmo* does not have the *ZX_RIGHT_MAP*, or
*options* contained a permissions flag corresponding to a right that *vmo* does not have.

**ZX_ERR_INVALID_ARGS** *options* is 0 or contains an undefined flag, either *addrs* or *pmt*
is not a valid pointer, *addrs_count* is not the same as the number of entries that would be
returned, or *offset* or *size* is not page-aligned.

**ZX_ERR_OUT_OF_RANGE** *offset* + *size* is out of the bounds of *vmo*.

**ZX_ERR_UNAVAILABLE** (Temporary) At least one page in the requested range could
not be pinned at this time.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[bti_create](bti_create.md),
[pmt_unpin](pmt_unpin.md),
[object_get_info](object_get_info.md).
