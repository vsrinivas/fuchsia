// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PENDING_EXCEPTION_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PENDING_EXCEPTION_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <string>

namespace forensics {
namespace exceptions {

// Holds a zx::exception and resets it if it is not handled within a period of time.
class PendingException {
 public:
  PendingException(async_dispatcher_t* dispatcher, zx::duration ttl, zx::exception exception);

  zx::exception&& TakeException();
  zx::process&& TakeProcess();
  zx::thread&& TakeThread();

 private:
  void Reset();

  zx::exception exception_;
  zx::process process_;
  zx::thread thread_;

  async::TaskClosureMethod<PendingException, &PendingException::Reset> reset_{this};
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PENDING_EXCEPTION_H_
