// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "src/lib/fxl/logging.h"

// This binary is meant to be a test ground for thread signaling.
// The first iteration shows how ZX_THREAD_SUSPENDED and ZX_THREAD_RUNNING
// signals are used.
//
// No code should depends on this, but rather is meant to be a sandox for
// zxdb developers.

namespace {

static constexpr char kBeacon[] = "Counter: Thread running.\n";

// This is the code that the new thread will run.
// It's meant to be an eternal loop.
int ThreadFunction(void*) {
  while (true) {
    // We use write to avoid deadlocking with the outside libc calls.
    write(1, kBeacon, sizeof(kBeacon));
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  }

  return 0;
}

}  // namespace

int main() {
  FXL_LOG(INFO) << "****** Creating thread.";

  thrd_t thread;
  thrd_create(&thread, ThreadFunction, nullptr);
  zx_handle_t thread_handle = 0;
  thread_handle = thrd_get_zx_handle(thread);

  FXL_LOG(INFO) << "****** Suspending thread.";

  zx_status_t status;
  zx_handle_t suspend_token;
  status = zx_task_suspend(thread_handle, &suspend_token);
  FXL_DCHECK(status == ZX_OK) << "Could not suspend thread: " << status;

  {
    zx_signals_t observed;
    status = zx_object_wait_one(thread_handle, ZX_THREAD_SUSPENDED, zx_deadline_after(ZX_SEC(1)),
                                &observed);
    FXL_DCHECK(status == ZX_OK) << "Could not get suspended signal: " << status;
    FXL_DCHECK((observed & ZX_THREAD_SUSPENDED) != 0);
  }

  // Resuming the thread should get a signal.
  FXL_LOG(INFO) << "****** Resuming thread.";
  zx_handle_close(suspend_token);
  {
    zx_signals_t observed;
    status = zx_object_wait_one(thread_handle, ZX_THREAD_RUNNING, zx_deadline_after(ZX_SEC(1)),
                                &observed);
    FXL_DCHECK(status == ZX_OK) << "Could not get running signal: " << status;
    FXL_DCHECK((observed & ZX_THREAD_RUNNING) != 0);
  }

  FXL_LOG(INFO) << "****** Success.";
}
