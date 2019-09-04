// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_
#define SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace fuchsia {
namespace exception {

// ExceptionBroker is meant to distribute exceptions according to some configuration.
// This enables the system to decides upon different exception handlers. In normal cases, standard
// crash reporting will occur, but the broker can be used to make other systems handle exceptions,
// such as debuggers.

class ExceptionBroker : public Handler {
 public:
  static std::unique_ptr<ExceptionBroker> Create(async_dispatcher_t* dispatcher,
                                                 std::shared_ptr<sys::ServiceDirectory> services);

  void OnException(zx::exception exception, ExceptionInfo info, OnExceptionCallback) override;

  fxl::WeakPtr<ExceptionBroker> GetWeakPtr();

  const std::map<uint64_t, fuchsia::crash::AnalyzerPtr>& connections() const {
    return connections_;
  }

 private:
  ExceptionBroker(std::shared_ptr<sys::ServiceDirectory> services);

  std::shared_ptr<sys::ServiceDirectory> services_;

  // As we create a new connection each time an exception is passed on to us, we need to
  // keep track of all the current outstanding connections.
  // These will be deleted as soon as the call returns or fails.
  std::map<uint64_t, fuchsia::crash::AnalyzerPtr> connections_;
  uint64_t next_connection_id_ = 1;

  fxl::WeakPtrFactory<ExceptionBroker> weak_factory_;
};

}  // namespace exception
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_EXCEPTION_BROKER_EXCEPTION_BROKER_H_
