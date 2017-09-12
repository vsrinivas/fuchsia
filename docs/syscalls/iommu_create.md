# zx_iommu_create

## NAME

iommu_create - create a new IOMMU object in the kernel

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_iommu_create(zx_handle_t root_rsrc, uint32_t type,
                            const void* desc, uint32_t desc_len, zx_handle_t* out);
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

**ZX_ERR_BAD_HANDLE**  *root_rsrc* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *root_rsrc* is not a resource handle.

**ZX_ERR_ACCESS_DENIED**  *root_rsrc* handle does not have sufficient privileges.

**ZX_ERR_NOT_SUPPORTED** *type* is not a defined value or is not
supported on this system.

**ZX_ERR_INVALID_ARGS**  *desc_len* is larger than *ZX_IOMMU_MAX_DESC_LEN*,
*desc* is an invalid pointer, *out* is an invalid pointer, or the contents of
*desc* are not valid for the given *type*.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

None yet.
