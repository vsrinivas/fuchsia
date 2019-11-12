#include "watchdog.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <zircon/assert.h>

#include <ddk/debug.h>

namespace display {

int Watchdog::Run() {
  ZX_ASSERT(dispatcher_);
  Reset();
  while (true) {
    zx_nanosleep((reset_time_.load() + delay_).get());
    if (!running_) {
      return 0;
    }
    if (async::Now(dispatcher_) - reset_time_.load() > delay_) {
      Crash();
    }
  }
}

void Watchdog::Stop() { running_ = false; }

void Watchdog::Reset() { reset_time_.store(async::Now(dispatcher_)); }

void Watchdog::Crash() {
  // Always log, even in production builds.
  zxlogf(ERROR, "watchdog fired: %s\n", message_);
  ZX_DEBUG_ASSERT(false);
}

}  // namespace display
