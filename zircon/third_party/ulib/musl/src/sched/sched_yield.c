#include <sched.h>
#include <zircon/syscalls.h>

int sched_yield(void) {
  _zx_thread_legacy_yield(0ul);
  return 0;
}
