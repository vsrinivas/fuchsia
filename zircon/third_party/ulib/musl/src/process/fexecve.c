#include <errno.h>
#include <unistd.h>

int fexecve(int fd, char* const argv[], char* const envp[]) {
  errno = ENOSYS;
  return -1;
}
