# zx_cprng_add_entropy

## NAME

zx_cprng_add_entropy - Add entropy to the kernel CPRNG

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_cprng_add_entropy(const void* buffer, size_t len);
```

## DESCRIPTION

**zx_cprng_add_entropy**() mixes the given entropy into the kernel CPRNG.
a privileged operation.  It will accept at most **ZX_CPRNG_ADD_ENTROPY_MAX_LEN**
bytes of entropy at a time.

## RETURN VALUE

**zx_cprng_add_entropy**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_INVALID_ARGS** *len* is too large, or *buffer* is not a valid
userspace pointer.

## BUGS

This syscall should be very privileged.
