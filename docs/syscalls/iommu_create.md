# zx_iommu_create

## NAME

iommu_create - create a new IOMMU object in the kernel

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_iommu_create(zx_handle_t root_resource, uint32_t type,
                            const void* desc, size_t desc_size, zx_handle_t* out);
```

## DESCRIPTION

**iommu_create**() creates a new object in the kernel representing an IOMMU device.

The value of *type* determines the interpretation of *desc*.  See below for
details about the values of *type*.

Upon success, a handle for the new IOMMU is returned.  This handle will have rights
**ZX_RIGHT_DUPLICATE** and **ZX_RIGHT_TRANSFER**.

### *type* = **ZX_IOMMU_TYPE_DUMMY**

This type represents a no-op IOMMU.  It provides no hardware-level protections
against unauthorized access to memory.  It does allow pinning of physical memory
pages, to prevent the reuse of a page until the driver using the page says it is
done with it.

*desc* must be a valid pointer to a value of type *zx_iommu_desc_dummy_t*.
*desc_len* must be *sizeof(zx_iommu_desc_dummy_t)*.

## RETURN VALUE

**iommu_create**() returns ZX_OK and a handle to the new IOMMU
(via *out*) on success.  In the event of failure, a negative error value
is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *root_resource* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *root_resource* is not a resource handle.

**ZX_ERR_ACCESS_DENIED**  *root_resource* handle does not have sufficient privileges.

**ZX_ERR_NOT_SUPPORTED** *type* is not a defined value or is not
supported on this system.

**ZX_ERR_INVALID_ARGS**  *desc_size* is larger than *ZX_IOMMU_MAX_DESC_LEN*,
*desc* is an invalid pointer, *out* is an invalid pointer, or the contents of
*desc* are not valid for the given *type*.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[bti_create](bti_create.md),
[bti_pin](bti_pin.md),
[pmt_unpin](pmt_unpin.md).
