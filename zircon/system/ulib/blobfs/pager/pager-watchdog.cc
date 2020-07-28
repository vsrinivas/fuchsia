#include "pager-watchdog.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/status.h>

#include <fs/trace.h>

namespace blobfs {
namespace pager {

PagerWatchdog::PagerWatchdog(zx::duration duration) : duration_(duration) {}

zx::status<std::unique_ptr<PagerWatchdog>> PagerWatchdog::Create(zx::duration duration) {
  auto woof = std::unique_ptr<PagerWatchdog>(new PagerWatchdog(duration));

  // Start the watchdog thread.
  zx_status_t status = woof->loop_.StartThread("blobfs-pager-watchdog");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Could not start pager watchdog thread\n");
    return zx::error(status);
  }

  return zx::ok(std::move(woof));
}

PagerWatchdog::ArmToken::ArmToken(PagerWatchdog* owner, async_dispatcher_t* dispatcher,
                                  zx::duration duration)
    : owner_(owner) {
  auto status = deadline_missed_task_.PostDelayed(dispatcher, duration);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: watchdog: Failed to arm watchdog timer: %s\n",
                   zx_status_get_string(status));
  }
}

void PagerWatchdog::ArmToken::OnDeadlineMissed() { owner_->OnDeadlineMissed(); }

PagerWatchdog::ArmToken PagerWatchdog::Arm() {
  // Called from the pager thread. Should avoid blocking.
  return ArmToken(this, loop_.dispatcher(), duration_);
}

void PagerWatchdog::OnDeadlineMissed() {
  if (callback_) {
    (*callback_)();
  } else {
    backtrace_request();
    FS_TRACE_ERROR(
        "blobfs: pager exceeded deadline of %lu s. It is likely that other threads on the system\n"
        "are stalled on page fault requests.\n",
        duration_.to_secs());
  }
}

}  // namespace pager
}  // namespace blobfs
