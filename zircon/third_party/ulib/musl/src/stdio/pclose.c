#include "stdio_impl.h"
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

int pclose(FILE* f) {
    pid_t pid = f->pipe_pid;
    fclose(f);
    return waitpid(pid, NULL, 0);
}
