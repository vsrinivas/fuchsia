#include <errno.h>
#include <sys/stat.h>

int mknodat(int fd, const char* path, mode_t mode, dev_t dev) {
  errno = ENOSYS;
  return -1;
}
