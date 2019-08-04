#include <errno.h>
#include <sys/wait.h>

int waitid(idtype_t type, id_t id, siginfo_t* info, int options) {
  errno = ENOSYS;
  return -1;
}
