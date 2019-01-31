#include "threads_impl.h"

int pthread_mutex_init(pthread_mutex_t* restrict m, const pthread_mutexattr_t* restrict a) {
    *m = (pthread_mutex_t){};
    if (a)
        m->_m_type = a->__attr;
    return 0;
}
