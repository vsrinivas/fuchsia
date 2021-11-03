// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using CrashReporterBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback, CrashReporter);

class CrashReporter : public CrashReporterBase {
 public:
  struct Expectations {
    std::string crash_signature;
    std::string reboot_log;
    std::optional<zx::duration> uptime;
    std::optional<bool> is_fatal;
  };

  CrashReporter(Expectations expectations) : expectations_(expectations) {}

  ~CrashReporter();

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

 private:
  const Expectations expectations_;

  std::string crash_signature_;
  std::string reboot_log_;
  std::optional<zx::duration> uptime_;
  std::optional<bool> is_fatal_;
};

class CrashReporterClosesConnection : public CrashReporterBase {
 public:
  // |fuchsia::feedback::CrashReporter|
  STUB_METHOD_CLOSES_CONNECTION(File, fuchsia::feedback::CrashReport, FileCallback)
};

class CrashReporterAlwaysReturnsError : public CrashReporterBase {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

class CrashReporterNoFileExpected : public CrashReporterBase {
 public:
  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CRASH_REPORTER_H_
