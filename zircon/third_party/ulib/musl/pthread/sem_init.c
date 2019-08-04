#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <stdatomic.h>

int sem_init(sem_t* sem, int pshared, unsigned value) {
  if (pshared) {
    errno = ENOSYS;
    return -1;
  }
  if (value > SEM_VALUE_MAX) {
    errno = EINVAL;
    return -1;
  }
  atomic_store(&sem->_s_value, value);
  atomic_store(&sem->_s_waiters, 0);
  return 0;
}
