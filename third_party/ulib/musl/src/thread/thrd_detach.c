#include <threads.h>

#include "pthread_impl.h"

int thrd_detach(thrd_t t) {
    switch (__pthread_detach(t)) {
    case 0:
        return thrd_success;
    default:
        return thrd_error;
    }
}
