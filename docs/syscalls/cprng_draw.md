# zx_cprng_draw

## NAME

zx_cprng_draw - Draw from the kernel's CPRNG

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_cprng_draw(void* buffer, size_t len, size_t* actual);
```

## DESCRIPTION

**zx_cprng_draw**() draws random bytes from the kernel CPRNG.  This data should be
suitable for cryptographic applications.  It will return at most
**ZX_CPRNG_DRAW_MAX_LEN** bytes at a time.

## RETURN VALUE

**zx_cprng_draw**() returns ZX_OK and the number of random bytes
drawn into *buffer* (via *actual) on success.

## ERRORS

**ZX_ERR_INVALID_ARGS** *len* is too large, or *buffer* or *actual* is
not a valid userspace pointer.

## NOTES

There are no other error conditions.  If its arguments are valid,
**zx_cprng_draw**() will succeed.

## EXAMPLES

```
// Draw |len| bytes of cryptographically secure random data into |buf|.
// It is not recommended to call this with large lengths.  If you need many
// bytes, you likely want a usermode CPRNG seeded by this function.
zx_status_t draw(char* buf, size_t len) {
    // This loop is necessary to deal with short reads from the kernel.
    while (len > 0) {
        size_t actual;
        zx_status_t status = zx_cprng_draw(buf, min(len, ZX_CPRNG_DRAW_MAX_LEN), &actual);
        if (status != ZX_OK) {
            return status;
        }
        buf += actual;
        len -= actual;
    }
    return ZX_OK;
}
```

## BUGS

This syscall should be rate-limited.
