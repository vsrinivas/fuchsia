#include <errno.h>
#include <pthread.h>

int pthread_barrier_init(pthread_barrier_t* restrict b, const pthread_barrierattr_t* restrict a,
                         unsigned count) {
  if (count == 0)
    return EINVAL;
  *b = (pthread_barrier_t){._b_limit = count};
  return 0;
}
