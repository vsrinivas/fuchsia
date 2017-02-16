#include "libc.h"
#include "pthread_impl.h"
#include <string.h>

void __reset_tls(void) {
    pthread_t self = __pthread_self();
    struct tls_module* p;
    size_t i, n = (size_t)self->head.dtv[0];
    if (n)
        for (p = libc.tls_head, i = 1; i <= n; i++, p = p->next) {
            if (!self->head.dtv[i])
                continue;
            memcpy(self->head.dtv[i], p->image, p->len);
            memset((char*)self->head.dtv[i] + p->len, 0, p->size - p->len);
        }
}
