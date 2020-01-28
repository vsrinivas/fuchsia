#include "atomic.h"
#include "threads_impl.h"

int __pthread_mutex_trylock_owner(pthread_mutex_t* m) {
  int old;
  int type = pthread_mutex_get_type(m);
  pid_t tid = __thread_get_tid();
  pid_t own;

  old = atomic_load(&m->_m_lock);
  own = pthread_mutex_state_to_tid(old);
  if (own == tid && (type == PTHREAD_MUTEX_RECURSIVE)) {
    if ((unsigned)m->_m_count >= INT_MAX)
      return EAGAIN;
    m->_m_count++;
    return 0;
  }

  int owned_state = pthread_mutex_tid_to_uncontested_state(tid);
  if (old || a_cas_shim(&m->_m_lock, old, owned_state) != old) {
    return EBUSY;
  }

  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
  int type = pthread_mutex_get_type(m);

  if (type == PTHREAD_MUTEX_NORMAL) {
    int owned_state = pthread_mutex_tid_to_uncontested_state(__thread_get_tid());
    return (a_cas_shim(&m->_m_lock, 0, owned_state) == 0) ? 0 : EBUSY;
  }

  return __pthread_mutex_trylock_owner(m);
}
