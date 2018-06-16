# zx_cprng_draw_new

## NAME

zx_cprng_draw_new - Draw from the kernel's CPRNG

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_cprng_draw_new(void* buffer, size_t buffer_size);
```

## DESCRIPTION

**zx_cprng_draw_new**() draws random bytes from the kernel CPRNG.  This data should be
suitable for cryptographic applications.  It will return at most
**ZX_CPRNG_DRAW_MAX_LEN** bytes at a time.

## RETURN VALUE

**zx_cprng_draw_new**() returns ZX_OK on success.

## ERRORS

**ZX_ERR_INVALID_ARGS** *buffer_size* is too large or *buffer* is not a valid
userspace pointer.

## NOTES

There are no other error conditions.  If its arguments are valid,
**zx_cprng_draw_new**() will succeed.

## EXAMPLES

```
// Draw |len| bytes of cryptographically secure random data into |buf|.
// It is not recommended to call this with large lengths.  If you need many
// bytes, you likely want a usermode CPRNG seeded by this function.
zx_status_t draw(char* buf, size_t len) {
    while (len > 0) {
        size_t n = min(len, ZX_CPRNG_DRAW_MAX_LEN);
        zx_status_t status = zx_cprng_draw_new(buf, n);
        if (status != ZX_OK) {
            return status;
        }
        buf += n;
        len -= n;
    }
    return ZX_OK;
}
```

## BUGS

This syscall should be rate-limited.
