#include "pthread_impl.h"

int* __errno_location(void) {
    return &__thrd_current()->errno_value;
}
