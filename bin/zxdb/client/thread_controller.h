// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Thread;

// Abstract base class that provides the policy decisions for various types of
// thread stepping.
class ThreadController {
 public:
  enum StopOp { kContinue, kStop };

  // The passed-in thread must outlive this class. Normally the controller will
  // be owned by the Thread object.
  explicit ThreadController(Thread* thread);

  virtual ~ThreadController();

  // Notification that the thread has stopped. The return value indicates what
  // the thread should do in response.
  //
  // If the ThreadController returns |kStop|, its assumed the controller has
  // completed its job and it will be deleted.
  virtual StopOp OnThreadStop(debug_ipc::NotifyException::Type stop_type,
        const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) = 0;

 protected:
  Thread* thread() { return thread_; }

  // Tells the owner of this class that this ThreadController has completed
  // its work. Normally returning kStop from OnThreadStop() will do this, but
  // if the controller has another way to get events (like breakpoints), it
  // may notice out-of-band that its work is done.
  //
  // This function will likely cause |this| to be deleted.
  void NotifyControllerDone();

 private:
  Thread* thread_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadController);
};

}  // namespace zxdb
