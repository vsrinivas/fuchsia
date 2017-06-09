# mx_vmar_protect

## NAME

vmar_protect - set protection of virtual memory pages

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmar_protect(mx_handle_t vmar_handle,
                            uintptr_t addr, size_t len,
                            uint32_t prot_flags);
```

## DESCRIPTION

**vmar_protect**() alters the access protections for the memory mappings
in the range of *len* bytes starting from *addr*. The *prot_flags* argument should
be a bitwise-or of one or more of the following:
- **MX_VM_FLAG_PERM_READ**  Map as readable.  It is an error if *vmar*
  does not have *MX_VM_FLAG_CAN_MAP_READ* permissions or the *vmar* handle does
  not have the *MX_RIGHT_READ* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *MX_RIGHT_READ* right.
- **MX_VM_FLAG_PERM_WRITE**  Map as writable.  It is an error if *vmar*
  does not have *MX_VM_FLAG_CAN_MAP_WRITE* permissions or the *vmar* handle does
  not have the *MX_RIGHT_WRITE* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *MX_RIGHT_WRITE* right.
- **MX_VM_FLAG_PERM_EXECUTE**  Map as executable.  It is an error if *vmar*
  does not have *MX_VM_FLAG_CAN_MAP_EXECUTE* permissions or the *vmar* handle does
  not have the *MX_RIGHT_EXECUTE* right.  It is also an error if the VMO handle
  used to create the mapping did not have the *MX_RIGHT_EXECUTE* right.

If *len* is not page-aligned, it will be rounded up the next page boundary.

## RETURN VALUE

**vmar_protect**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *vmar_handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *vmar_handle* is not a VMAR handle.

**MX_ERR_INVALID_ARGS**  *prot_flags* is an unsupported combination of flags
(e.g., **PROT_WRITE** but not **PROT_READ**), *addr* is not page-aligned,
*len* is 0, or some subrange of the requested range is occupied by a subregion.

**MX_ERR_NOT_FOUND**  Some subrange of the requested range is not mapped.

**MX_ERR_ACCESS_DENIED**  *vmar_handle* does not have the proper rights for the
requested change, the original VMO handle used to create the mapping did not
have the rights for the requested change, or the VMAR itself does not allow
the requested change.

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_destroy](vmar_destroy.md),
[vmar_map](vmar_map.md),
[vmar_unmap](vmar_unmap.md).
