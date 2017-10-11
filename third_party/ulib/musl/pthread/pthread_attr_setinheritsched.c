#include "pthread_impl.h"

int pthread_attr_setinheritsched(pthread_attr_t* a, int inherit) {
    if (inherit != PTHREAD_INHERIT_SCHED &&
        inherit != PTHREAD_EXPLICIT_SCHED)
        return EINVAL;
    a->_a_sched = inherit;
    return 0;
}
