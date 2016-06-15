#include "pthread_impl.h"

int* __errno_location(void) {
    return mxr_tls_get(MXR_TLS_SLOT_ERRNO);
}
