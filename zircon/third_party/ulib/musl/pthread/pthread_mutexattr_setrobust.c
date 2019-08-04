#include "threads_impl.h"

int pthread_mutexattr_setrobust(pthread_mutexattr_t* a, int robust) {
  // Attempts to set bits outside of the mask are not allowed.
  if (robust & ~PTHREAD_MUTEX_ROBUST_MASK)
    return EINVAL;

  // We do not support robust pthread_mutex_ts.
  if (robust)
    return ENOTSUP;

  return 0;
}
