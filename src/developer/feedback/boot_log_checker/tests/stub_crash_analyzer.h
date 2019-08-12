// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_ANALYZER_H_
#define SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_ANALYZER_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

#include "src/lib/fxl/logging.h"

namespace feedback {

class StubCrashAnalyzer : public fuchsia::crash::Analyzer {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::crash::Analyzer> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia.crash.Analyzer|
  void OnNativeException(zx::process process, zx::thread thread,
                         OnNativeExceptionCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void OnManagedRuntimeException(std::string component_url,
                                 fuchsia::crash::ManagedRuntimeException exception,
                                 OnManagedRuntimeExceptionCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void OnKernelPanicCrashLog(fuchsia::mem::Buffer crash_log,
                             OnKernelPanicCrashLogCallback callback) override;

  const std::string& kernel_panic_crash_log() { return kernel_panic_crash_log_; };

 protected:
  void CloseAllConnections() { bindings_.CloseAll(); }

 private:
  fidl::BindingSet<fuchsia::crash::Analyzer> bindings_;
  std::string kernel_panic_crash_log_;
};

class StubCrashAnalyzerClosesConnection : public StubCrashAnalyzer {
 public:
  void OnKernelPanicCrashLog(fuchsia::mem::Buffer crash_log,
                             OnKernelPanicCrashLogCallback callback) override {
    CloseAllConnections();
  }
};

class StubCrashAnalyzerAlwaysReturnsError : public StubCrashAnalyzer {
 public:
  void OnKernelPanicCrashLog(fuchsia::mem::Buffer crash_log,
                             OnKernelPanicCrashLogCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_ANALYZER_H_
