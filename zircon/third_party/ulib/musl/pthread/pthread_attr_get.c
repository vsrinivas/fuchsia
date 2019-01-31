#include "threads_impl.h"

int pthread_attr_getdetachstate(const pthread_attr_t* a, int* state) {
    *state = a->_a_detach;
    return 0;
}
int pthread_attr_getguardsize(const pthread_attr_t* restrict a, size_t* restrict size) {
    *size = a->_a_guardsize;
    return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t* restrict a,
                               struct sched_param* restrict param) {
    param->sched_priority = a->_a_prio;
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t* restrict a, void** restrict addr,
                          size_t* restrict size) {
    if (a->_a_stackaddr == NULL)
        return EINVAL;
    *addr = a->_a_stackaddr;
    *size = a->_a_stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* restrict a, size_t* restrict size) {
    *size = a->_a_stacksize;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t* restrict a, clockid_t* restrict clk) {
    *clk = a->__attr & 0x7fffffff;
    return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t* restrict a, int* restrict protocol) {
    *protocol = PTHREAD_PRIO_NONE;
    return 0;
}
int pthread_mutexattr_getrobust(const pthread_mutexattr_t* restrict a, int* restrict robust) {
    // We do not support robust pthread_mutex_ts.
    *robust = 0;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* restrict a, int* restrict type) {
    *type = a->__attr & PTHREAD_MUTEX_MASK;
    return 0;
}
