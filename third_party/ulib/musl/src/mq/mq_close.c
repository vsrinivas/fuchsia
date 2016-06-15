#include "syscall.h"
#include <mqueue.h>

int mq_close(mqd_t mqd) {
    return syscall(SYS_close, mqd);
}
