#include "ipc.h"
#include "syscall.h"
#include <errno.h>
#include <limits.h>
#include <sys/sem.h>

int semget(key_t key, int n, int fl) {
    /* The kernel uses the wrong type for the sem_nsems member
     * of struct semid_ds, and thus might not check that the
     * n fits in the correct (per POSIX) userspace type, so
     * we have to check here. */
    if (n > USHRT_MAX) return __syscall_ret(-EINVAL);
    return syscall(SYS_semget, key, n, fl);
}
