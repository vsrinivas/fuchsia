#include "futex_impl.h"
#include "threads_impl.h"

int pthread_mutex_unlock(pthread_mutex_t* m) {
  int waiters = atomic_load(&m->_m_waiters);
  int type = pthread_mutex_get_type(m);
  int cont;

  if (type != PTHREAD_MUTEX_NORMAL) {
    if (pthread_mutex_state_to_tid(atomic_load(&m->_m_lock)) != __thread_get_tid())
      return EPERM;
    if ((type == PTHREAD_MUTEX_RECURSIVE) && m->_m_count)
      return m->_m_count--, 0;
  }

  // Cache this trait before we release the mutex
  bool prio_inherit = pthread_mutex_prio_inherit(m);

  // Release the mutex.
  cont = atomic_exchange(&m->_m_lock, 0);

  // At this point, the mutex was unlocked.  In some usage patterns
  // (e.g. for reference counting), another thread might now acquire the
  // mutex and free the memory containing it.  This means we must not
  // dereference |m| from this point onwards.

  if (waiters || pthread_mutex_is_state_contested(cont)) {
    // Note that the mutex's memory could have been freed and reused by
    // this point, so this could cause a spurious futex wakeup for a
    // unrelated user of the memory location.
    if (prio_inherit) {
      _zx_futex_wake_single_owner(&m->_m_lock);
    } else {
      _zx_futex_wake(&m->_m_lock, 1);
    }
  }
  return 0;
}
