// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/time.h>

#include <optional>

namespace feedback {

class StubCrashReporter : public fuchsia::feedback::CrashReporter {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::feedback::CrashReporter> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::CrashReporter>>(
          this, std::move(request));
    };
  }

  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

  const std::string& crash_signature() { return crash_signature_; };
  const std::string& reboot_log() { return reboot_log_; };
  const std::optional<zx::duration>& uptime() { return uptime_; };

 protected:
  void CloseConnection();

 private:
  std::unique_ptr<fidl::Binding<fuchsia::feedback::CrashReporter>> binding_;
  std::string crash_signature_;
  std::string reboot_log_;
  std::optional<zx::duration> uptime_;
};

class StubCrashReporterClosesConnection : public StubCrashReporter {
 public:
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override {
    CloseConnection();
  }
};

class StubCrashReporterAlwaysReturnsError : public StubCrashReporter {
 public:
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

class StubCrashReporterNoFileExpected : public StubCrashReporter {
 public:
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_BOOT_LOG_CHECKER_TESTS_STUB_CRASH_REPORTER_H_
