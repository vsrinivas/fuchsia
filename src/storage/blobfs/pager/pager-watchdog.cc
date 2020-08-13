#include "pager-watchdog.h"

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/status.h>

#include <fs/trace.h>

namespace blobfs {
namespace pager {

PagerWatchdog::PagerWatchdog(zx::duration duration) : duration_(duration) {
  thread_ = std::thread(&PagerWatchdog::Thread, this);
}

PagerWatchdog::~PagerWatchdog() {
  {
    std::scoped_lock lock(mutex_);
    terminate_ = true;
  }
  condition_.notify_all();
  thread_.join();
}

void PagerWatchdog::Thread() {
  for (;;) {
    int deadline_missed = 0;
    {
      std::scoped_lock lock(mutex_);
      if (terminate_) {
        return;
      }
      // See if any deadlines have been exceeded. The order of the list means we only need to check
      // the end.
      zx::ticks now = zx::ticks::now();
      if (token_ && token_->deadline() <= now) {
        ++deadline_missed;
        token_ = nullptr;
      }
      // If none, wait for the next deadline to expire.
      if (deadline_missed == 0) {
        if (token_) {
          condition_.wait_for(mutex_,
                              std::chrono::nanoseconds((token_->deadline() - now) * 1'000'000'000 /
                                                       zx::ticks::per_second()));
        } else {
          condition_.wait(mutex_);
        }
      }
    }
    // Handle any deadlines that have been missed (outside of the lock).
    if (deadline_missed > 0) {
      OnDeadlineMissed(deadline_missed);
      condition_.notify_all();
    }
  }
}

PagerWatchdog::ArmToken PagerWatchdog::ArmWithDuration(zx::duration duration) {
  // Called from the pager thread. Should avoid blocking.
  return ArmToken(*this, duration);
}

void PagerWatchdog::OnDeadlineMissed(int count) {
  if (callback_) {
    (*callback_)(count);
  } else {
    backtrace_request();
    FS_TRACE_ERROR(
        "blobfs: pager exceeded deadline of %lu s for %u request(s). It is likely that other "
        "threads on the system\n"
        "are stalled on page fault requests.\n",
        duration_.to_secs(), count);
  }
}

void PagerWatchdog::RunUntilIdle() {
  std::scoped_lock lock(mutex_);
  while (token_ != nullptr)
    condition_.wait(mutex_);
}

PagerWatchdog::ArmToken::ArmToken(PagerWatchdog& watchdog, zx::duration duration)
    : watchdog_(watchdog),
      deadline_(zx::ticks::now() + zx::ticks::per_second() * duration.to_nsecs() / 1'000'000'000) {
  std::scoped_lock lock(watchdog_.mutex_);
  ZX_ASSERT(watchdog_.token_ == nullptr);
  watchdog_.token_ = this;
  watchdog_.condition_.notify_all();
}

PagerWatchdog::ArmToken::~ArmToken() {
  std::scoped_lock lock(watchdog_.mutex_);
  watchdog_.token_ = nullptr;
  watchdog_.condition_.notify_all();
}

}  // namespace pager
}  // namespace blobfs
