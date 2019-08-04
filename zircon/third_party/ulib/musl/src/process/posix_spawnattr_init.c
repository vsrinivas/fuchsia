#include <spawn.h>

int posix_spawnattr_init(posix_spawnattr_t* attr) {
  *attr = (posix_spawnattr_t){};
  return 0;
}
