// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_
#define SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace fuchsia {
namespace exception {

// ExceptionBroker is meant to distribute exceptions according to some configuration.
// This enables the system to decides upon different exception handlers. In normal cases, standard
// crash reporting will occur, but the broker can be used to make other systems handle exceptions,
// such as debuggers.

class ExceptionBroker : public Handler, public ProcessLimbo {
 public:
  static std::unique_ptr<ExceptionBroker> Create(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services);

  // fuchsia.exception.Handler implementation.

  void OnException(zx::exception exception, ExceptionInfo info, OnExceptionCallback) override;

  // fuchsia.exception.ProcessLimbo implementation.

  void GetProcessesWaitingOnException(GetProcessesWaitingOnExceptionCallback) override;

  fxl::WeakPtr<ExceptionBroker> GetWeakPtr();

  const std::map<uint64_t, fuchsia::feedback::CrashReporterPtr>& connections() const {
    return connections_;
  }

  bool use_limbo() const { return use_limbo_; }
  void set_use_limbo(bool use_limbo) { use_limbo_ = use_limbo; }

 private:
  void FileCrashReport(ProcessException);     // |use_limbo_| == false.
  void AddToLimbo(ProcessException);          // |use_limbo_| == true.

  ExceptionBroker(std::shared_ptr<sys::ServiceDirectory> services);

  std::shared_ptr<sys::ServiceDirectory> services_;

  // As we create a new connection each time an exception is passed on to us, we need to
  // keep track of all the current outstanding connections.
  // These will be deleted as soon as the call returns or fails.
  std::map<uint64_t, fuchsia::feedback::CrashReporterPtr> connections_;
  uint64_t next_connection_id_ = 1;

  // TODO(donosoc): This is an extremely naive approach.
  //                There are several policies to make this more robust:
  //                - Put a ceiling on the amount of exceptions to be held.
  //                - Define an eviction policy (FIFO probably).
  //                - Set a timeout for exceptions (configurable).
  //                - Decide on a throttle mechanism (if the same process is crashing continously).
  std::vector<ProcessException> limbo_;

  // TODO(donosoc): This should be moved into reading a config file at startup.
  //                Exposed for testing purposes.
  bool use_limbo_ = false;

  fxl::WeakPtrFactory<ExceptionBroker> weak_factory_;
};

}  // namespace exception
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_
