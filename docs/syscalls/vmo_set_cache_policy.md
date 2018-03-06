# zx_vmo_op_range

## NAME

vmo_set_cache_policy - set the caching policy for pages held by a VMO.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t vmo_set_cache_policy(zx_handle_t handle, uint32_t cache_policy);

```

## DESCRIPTION

**vmo_set_cache_policy()** sets caching policy for a VMO. Generally used on VMOs
that point directly at physical memory. Such VMOs are generally only handed to
userspace via bus protocol interfaces, so this syscall will typically only be
used by drivers dealing with device memory. This call can also be used on a
regular memory backed VMO with similar limitations and uses.

A handle must have the *ZX_RIGHT_MAP* right for this call to be
permitted. Additionally, the VMO must not presently be mapped by any process,
be cloned, be a clone itself, or have any memory committed.

*cache_policy* cache flags to use:

**ZX_CACHE_POLICY_CACHED** - Use hardware caching.

**ZX_CACHE_POLICY_UNCACHED** - Disable caching.

**ZX_CACHE_POLICY_UNCACHED_DEVICE** - Disable cache and treat as device memory.
This is architecture dependent and may be equivalent to
*ZX_CACHE_POLICY_UNCACHED* on some architectures.

**ZX_CACHE_POLICY_WRITE_COMBINING** - Uncached with write combining.

## RETURN VALUE

**vmo_set_cache_policy()** returns **ZX_OK** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** Cache policy has been configured for this VMO already and
may not be changed, or *handle* lacks the ZX_RIGHT_MAP right.

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_INVALID_ARGS** *cache_policy* contains flags outside of the ones listed
above, or *cache_policy* contains an invalid mix of cache policy flags.

**ZX_ERR_NOT_SUPPORTED** The VMO *handle* corresponds to is not one holding
physical memory.

**ZX_ERR_BAD_STATE** Cache policy cannot be changed because the VMO is presently
mapped, cloned, a clone itself, or have any memory committed.

## SEE ALSO

[vmo_create](vmo_create.md), [vmo_read](vmo_read.md), [vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md), [vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
