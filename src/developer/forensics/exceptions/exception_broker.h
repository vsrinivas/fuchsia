// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_BROKER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_BROKER_H_

#include "src/developer/forensics/exceptions/process_limbo_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace forensics {
namespace exceptions {

// ExceptionBroker is meant to distribute exceptions according to some configuration.
// This enables the system to decides upon different exception handlers. In normal cases, standard
// crash reporting will occur, but the broker can be used to make other systems handle exceptions,
// such as debuggers.

class ExceptionBroker : public fuchsia::exception::Handler {
 public:
  // If |override_filepath| is defined, it will attempt to locate that file instead of the default
  // config one. See exception_broker.cc for the prod filepath.
  static std::unique_ptr<ExceptionBroker> Create(const char* override_filepath = nullptr);

  // fuchsia.exception.Handler implementation.

  void OnException(zx::exception exception, fuchsia::exception::ExceptionInfo info,
                   OnExceptionCallback) override;

  ProcessLimboManager& limbo_manager() { return limbo_manager_; }
  const ProcessLimboManager& limbo_manager() const { return limbo_manager_; }

 private:
  ProcessLimboManager limbo_manager_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_EXCEPTION_BROKER_H_
