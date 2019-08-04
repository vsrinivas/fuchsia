#include "threads_impl.h"

int pthread_mutexattr_setprotocol(pthread_mutexattr_t* a, int protocol) {
  // Attempts to set bits outside of the mask are not allowed.
  if (protocol & ~PTHREAD_MUTEX_PROTOCOL_MASK)
    return EINVAL;

  // We do not support the PRIO_PROTECT protocol
  if (protocol & PTHREAD_PRIO_PROTECT)
    return ENOTSUP;

  a->__attr = (a->__attr & ~(PTHREAD_MUTEX_PROTOCOL_MASK << PTHREAD_MUTEX_PROTOCOL_SHIFT)) |
              (protocol << PTHREAD_MUTEX_PROTOCOL_SHIFT);

  return 0;
}
