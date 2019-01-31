#include <sys/random.h>

#include <assert.h>
#include <errno.h>
#include <zircon/syscalls.h>

#define MAX_LENGTH 256

static_assert(MAX_LENGTH <= ZX_CPRNG_DRAW_MAX_LEN, "");

int getentropy(void* buffer, size_t length) {
    if (length > MAX_LENGTH) {
        errno = EIO;
        return -1;
    }

    _zx_cprng_draw(buffer, length);
    return 0;
}
