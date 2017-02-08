#include "libc.h"
#include "pthread_impl.h"
#include <string.h>

void* __copy_tls(unsigned char* mem) {
    pthread_t td;
    struct tls_module* p;
    size_t i;
    void** dtv;

#ifdef TLS_ABOVE_TP
    dtv = (void**)(mem + libc.tls_size) - (libc.tls_cnt + 1);

    mem += -((uintptr_t)mem + sizeof(struct pthread)) & (libc.tls_align - 1);
    td = (pthread_t)mem;
    mem += sizeof(struct pthread);

    for (i = 1, p = libc.tls_head; p; i++, p = p->next) {
        dtv[i] = mem + p->offset;
        memcpy(dtv[i], p->image, p->len);
    }
#else
    dtv = (void**)mem;

    mem += libc.tls_size - sizeof(struct pthread);
    mem -= (uintptr_t)mem & (libc.tls_align - 1);
    td = (pthread_t)mem;

    for (i = 1, p = libc.tls_head; p; i++, p = p->next) {
        dtv[i] = mem - p->offset;
        memcpy(dtv[i], p->image, p->len);
    }
#endif
    dtv[0] = (void*)libc.tls_cnt;
    td->dtv = td->dtv_copy = dtv;
    return td;
}
