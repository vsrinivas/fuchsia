#include "libc.h"
#include "pthread_impl.h"

static void dummy(void) {}

weak_alias(dummy, __testcancel);

void __pthread_testcancel(void) {
    __testcancel();
}

weak_alias(__pthread_testcancel, pthread_testcancel);
