#define _GNU_SOURCE
#include "fdop.h"
#include "libc.h"
#include "threads_impl.h"
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
