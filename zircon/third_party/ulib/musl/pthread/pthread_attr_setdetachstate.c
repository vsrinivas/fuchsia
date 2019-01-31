#include "threads_impl.h"

int pthread_attr_setdetachstate(pthread_attr_t* a, int state) {
    if (state != PTHREAD_CREATE_DETACHED &&
        state != PTHREAD_CREATE_JOINABLE)
        return EINVAL;
    a->_a_detach = state;
    return 0;
}
