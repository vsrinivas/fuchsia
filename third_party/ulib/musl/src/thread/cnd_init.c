#include <threads.h>

int cnd_init(cnd_t* c) {
    *c = (cnd_t){};
    return thrd_success;
}
