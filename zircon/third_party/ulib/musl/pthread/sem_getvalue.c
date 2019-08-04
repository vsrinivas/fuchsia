#include <semaphore.h>
#include <stdatomic.h>

int sem_getvalue(sem_t* restrict sem, int* restrict valp) {
  int val = atomic_load(&sem->_s_value);
  *valp = val < 0 ? 0 : val;
  return 0;
}
