// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_listener.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace escher {

FenceListener::FenceListener(zx::event fence)
    : fence_(std::move(fence)),
      waiter_(fence_.get(),     // handle
              kFenceSignalled)  // trigger
{
  FX_DCHECK(fence_);
}

bool FenceListener::WaitReady(zx::duration timeout) {
  zx::time zx_deadline;
  if (timeout <= zx::nsec(0))
    zx_deadline = zx::time();
  else if (timeout == zx::duration::infinite())
    zx_deadline = zx::time::infinite();
  else
    zx_deadline = zx::deadline_after(timeout);

  zx_signals_t pending = 0u;
  while (!ready_) {
    zx_status_t status = fence_.wait_one(kFenceSignalled, zx_deadline, &pending);
    FX_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
    ready_ = pending & kFenceSignalled;
    if (zx_deadline != zx::time::infinite())
      break;
  }
  return ready_;
}

void FenceListener::WaitReadyAsync(fit::closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FX_DCHECK(!ready_callback_);

  waiter_.set_handler(std::bind(&FenceListener::OnFenceSignalled, this, std::placeholders::_3,
                                std::placeholders::_4));
  zx_status_t status = waiter_.Begin(async_get_default_dispatcher());
  FX_CHECK(status == ZX_OK);
  ready_callback_ = std::move(ready_callback);
}

void FenceListener::OnFenceSignalled(zx_status_t status, const zx_packet_signal* signal) {
  if (status == ZX_OK) {
    zx_signals_t pending = signal->observed;
    FX_DCHECK(pending & kFenceSignalled);
    FX_DCHECK(ready_callback_);

    ready_ = true;
    fit::closure callback = std::move(ready_callback_);
    waiter_.Cancel();

    callback();
  } else {
    FX_LOGS(ERROR) << "FenceListener::OnFenceSignalled received an "
                      "error status code: "
                   << status;

    // TODO(fxbug.dev/23426): Close the session if there is an error, or if the fence
    // is closed.
  }
}

}  // namespace escher
