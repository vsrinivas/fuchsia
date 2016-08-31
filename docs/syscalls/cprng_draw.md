# mx_cprng_draw

## NAME

mx_cprng_draw - Draw from the kernel's CPRNG

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_ssize_t mx_cprng_draw(void* buffer, mx_size_t len);
```

## DESCRIPTION

**mx_cprng_draw**() draws random bytes from the kernel CPRNG.  This data should be
suitable for cryptographic applications.  It will return at most
**MX_CPRNG_DRAW_MAX_LEN** bytes at a time.

## RETURN VALUE

**mx_cprng_draw**() returns the number of random bytes drawn into *buffer*.

## ERRORS

**ERR_INVALID_ARGS**  *len* is too large.

**ERR_INVALID_ARGS**  *buffer* is not a valid user space pointer.

There are no other error conditions.  If its arguments are valid,
**mx_cprng_draw**() will succeed.

## BUGS

This syscall should be rate-limited.
