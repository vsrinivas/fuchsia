#include <errno.h>
#include <sys/inotify.h>

#include "libc.h"

int __inotify_init(void) {
  errno = ENOSYS;
  return -1;
}

int __inotify_init1(int flags) {
  errno = ENOSYS;
  return -1;
}

int __inotify_add_watch(int fd, const char* pathname, uint32_t mask) {
  errno = ENOSYS;
  return -1;
}

int __inotify_rm_watch(int fd, int wd) {
  errno = ENOSYS;
  return -1;
}

weak_alias(__inotify_init, inotify_init);
weak_alias(__inotify_init1, inotify_init1);
weak_alias(__inotify_add_watch, inotify_add_watch);
weak_alias(__inotify_rm_watch, inotify_rm_watch);
