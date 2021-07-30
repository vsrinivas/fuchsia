#include <lib/zircon-internal/unique-backtrace.h>
#include <stdlib.h>
#include <zircon/syscalls.h>

_Noreturn void abort(void) {
  for (;;) {
    CRASH_WITH_UNIQUE_BACKTRACE();
    _zx_process_exit(-1);
  }
}
