#include <pthread.h>

#include <assert.h>
#include <stdalign.h>
#include <threads.h>

static_assert(ONCE_FLAG_INIT == PTHREAD_ONCE_INIT, "");
static_assert(sizeof(pthread_once_t) == sizeof(once_flag), "");
static_assert(alignof(pthread_once_t) == alignof(once_flag), "");

int pthread_once(pthread_once_t* control, void (*init)(void)) {
    call_once(control, init);
    return 0;
}
