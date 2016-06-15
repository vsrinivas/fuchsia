#include "ipc.h"
#include "syscall.h"
#include <sys/shm.h>

int shmctl(int id, int cmd, struct shmid_ds* buf) {
    return syscall(SYS_shmctl, id, cmd | IPC_64, buf);
}
