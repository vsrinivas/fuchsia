#include "syscall.h"
#include <mqueue.h>

int mq_setattr(mqd_t mqd, const struct mq_attr* restrict new, struct mq_attr* restrict old) {
    return syscall(SYS_mq_getsetattr, mqd, new, old);
}
