#include "ipc.h"
#include "syscall.h"
#include <stdarg.h>
#include <sys/sem.h>

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

int semctl(int id, int num, int cmd, ...) {
    union semun arg = {0};
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
    return syscall(SYS_semctl, id, num, cmd | IPC_64, arg.buf);
}
