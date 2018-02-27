#include "threads_impl.h"

int pthread_attr_init(pthread_attr_t* a) {
    *a = DEFAULT_PTHREAD_ATTR;
    return 0;
}
