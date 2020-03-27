// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class CrashReporter : public fuchsia::feedback::testing::CrashReporter_TestBase {
 public:
  fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::CrashReporter>>(
          this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

  // |fuchsia::feedback::testing::CrashReporter_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

  const std::string& crash_signature() { return crash_signature_; };
  const std::string& reboot_log() { return reboot_log_; };
  const std::optional<zx::duration>& uptime() { return uptime_; };

 private:
  std::unique_ptr<fidl::Binding<fuchsia::feedback::CrashReporter>> binding_;
  std::string crash_signature_;
  std::string reboot_log_;
  std::optional<zx::duration> uptime_;
};

class CrashReporterClosesConnection : public CrashReporter {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override {
    CloseConnection();
  }
};

class CrashReporterAlwaysReturnsError : public CrashReporter {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

class CrashReporterNoFileExpected : public CrashReporter {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CRASH_REPORTER_H_
