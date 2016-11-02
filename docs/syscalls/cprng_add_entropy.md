# mx_cprng_add_entropy

## NAME

mx_cprng_add_entropy - Add entropy to the kernel CPRNG

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_cprng_add_entropy(const void* buffer, mx_size_t len);
```

## DESCRIPTION

**mx_cprng_add_entropy**() mixes the given entropy into the kernel CPRNG.
a privileged operation.  It will accept at most **MX_CPRNG_ADD_ENTROPY_MAX_LEN**
bytes of entropy at a time.

## RETURN VALUE

**mx_cprng_add_entropy**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *len* is too large.

**ERR_INVALID_ARGS**  *buffer* is not a valid user space pointer.

## BUGS

This syscall should be very privileged.
