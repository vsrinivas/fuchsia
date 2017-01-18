#include <sys/sem.h>

#include <errno.h>
#include <stdarg.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

int semctl(int id, int num, int cmd, ...) {
    union semun arg = {};
    va_list ap;
    switch (cmd) {
    case SETVAL:
    case GETALL:
    case SETALL:
    case IPC_STAT:
    case IPC_SET:
    case IPC_INFO:
    case SEM_INFO:
    case SEM_STAT:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
    }

    errno = ENOSYS;
    return -1;
}
