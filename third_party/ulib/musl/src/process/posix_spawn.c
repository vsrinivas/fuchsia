#define _GNU_SOURCE
#include "fdop.h"
#include "libc.h"
#include "pthread_impl.h"
#include <fcntl.h>
#include <sched.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

struct args {
    int p[2];
    sigset_t oldmask;
    const char* path;
    int (*exec)(const char*, char* const*, char* const*);
    const posix_spawn_file_actions_t* fa;
    const posix_spawnattr_t* restrict attr;
    char *const *argv, *const *envp;
};

static int child(void* args_vp) {
    int ret;
    struct args* args = args_vp;
    int p = args->p[1];
    const posix_spawn_file_actions_t* fa = args->fa;
    const posix_spawnattr_t* restrict attr = args->attr;

    close(args->p[0]);

    if (attr->__flags & POSIX_SPAWN_SETPGROUP)
        if ((ret = setpgid(0, attr->__pgrp)))
            goto fail;

    if (attr->__flags & POSIX_SPAWN_RESETIDS)
        if ((ret = setgid(getgid())) ||
            (ret = setuid(getuid())))
            goto fail;

    if (fa && fa->__actions) {
        struct fdop* op;
        int fd;
        for (op = fa->__actions; op->next; op = op->next)
            ;
        for (; op; op = op->prev) {
            /* It's possible that a file operation would clobber
             * the pipe fd used for synchronizing with the
             * parent. To avoid that, we dup the pipe onto
             * an unoccupied fd. */
            if (op->fd == p) {
                ret = dup(p);
                if (ret < 0)
                    goto fail;
                close(p);
                p = ret;
            }
            switch (op->cmd) {
            case FDOP_CLOSE:
                close(op->fd);
                break;
            case FDOP_DUP2:
                if ((ret = dup2(op->srcfd, op->fd)) < 0)
                    goto fail;
                break;
            case FDOP_OPEN:
                fd = open(op->path, op->oflag, op->mode);
                if (fd < 0) {
                    ret = errno;
                    goto fail;
                }
                if (fd != op->fd) {
                    if ((ret = dup2(fd, op->fd)) < 0)
                        goto fail;
                    close(fd);
                }
                break;
            }
        }
    }

    /* Close-on-exec flag may have been lost if we moved the pipe
     * to a different fd. We don't use F_DUPFD_CLOEXEC above because
     * it would fail on older kernels and atomicity is not needed --
     * in this process there are no threads or signal handlers. */
    fcntl(p, F_SETFD, FD_CLOEXEC);

    pthread_sigmask(SIG_SETMASK,
                    (attr->__flags & POSIX_SPAWN_SETSIGMASK) ? &attr->__mask : &args->oldmask, 0);

    args->exec(args->path, args->argv, args->envp);
    ret = -errno;

fail:
    /* Since sizeof errno < PIPE_BUF, the write is atomic. */
    ret = -ret;
    if (ret)
        while (write(p, &ret, sizeof(ret)) < 0)
            ;
    _exit(127);
}

int __posix_spawnx(pid_t* restrict res, const char* restrict path,
                   int (*exec)(const char*, char* const*, char* const*),
                   const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* restrict attr,
                   char* const argv[restrict], char* const envp[restrict]) {
    pid_t pid;
    int ec = 0;
    struct args args;

    if (pipe2(args.p, O_CLOEXEC))
        return errno;

    args.path = path;
    args.exec = exec;
    args.fa = fa;
    args.attr = attr ? attr : &(const posix_spawnattr_t){};
    args.argv = argv;
    args.envp = envp;
    pthread_sigmask(SIG_BLOCK, SIGALL_SET, &args.oldmask);

    // TODO(kulakowski) Launchpad up a process here.
    pid = -ENOSYS;
    close(args.p[1]);

    if (pid > 0) {
        if (read(args.p[0], &ec, sizeof ec) != sizeof ec)
            ec = 0;
        else
            waitpid(pid, &(int){0}, 0);
    } else {
        ec = -pid;
    }

    close(args.p[0]);

    if (!ec && res)
        *res = pid;

    pthread_sigmask(SIG_SETMASK, &args.oldmask, 0);

    return ec;
}

int posix_spawn(pid_t* restrict res, const char* restrict path,
                const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* restrict attr,
                char* const argv[restrict], char* const envp[restrict]) {
    return __posix_spawnx(res, path, execve, fa, attr, argv, envp);
}
