#include <errno.h>
#include <unistd.h>

int execve(const char* path, char* const argv[], char* const envp[]) {
  errno = ENOSYS;
  return -1;
}
