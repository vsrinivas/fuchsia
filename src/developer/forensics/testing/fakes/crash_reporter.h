// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_FAKES_CRASH_REPORTER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_FAKES_CRASH_REPORTER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/testing/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include <list>
#include <memory>

namespace forensics {
namespace fakes {

class FakeCrashReporterQuerier;

// Fake handler for fuchsia.feedback.CrashReporter, returns an error if the filed crash report
// doesn't contain a program name. Otherwise, an ok reponse is returned.
class CrashReporter : public fuchsia::feedback::CrashReporter {
 public:
  void SetQuerier(
      fidl::InterfaceRequest<fuchsia::feedback::testing::FakeCrashReporterQuerier> request);
  void ResetQuerier();

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

 private:
  std::unique_ptr<FakeCrashReporterQuerier> querier_;
  size_t num_crash_reports_filed_{0};
};

class FakeCrashReporterQuerier : public fuchsia::feedback::testing::FakeCrashReporterQuerier {
 public:
  FakeCrashReporterQuerier(
      CrashReporter* crash_reporter,
      fidl::InterfaceRequest<fuchsia::feedback::testing::FakeCrashReporterQuerier> request,
      size_t num_crash_reports_filed);

  void UpdateAndNotify(size_t num_crash_reports_filed);

  // |fuchsia::feedback::testing::FakeCrashReporterQuerier|
  void WatchFile(WatchFileCallback callback) override;

 private:
  // If |callback_| has a value, execute it on the condition |watch_file_dirty_bit_| is set.
  void Notify();

  fidl::Binding<fuchsia::feedback::testing::FakeCrashReporterQuerier> connection_;
  std::optional<WatchFileCallback> callback_;

  size_t num_crash_reports_filed_;
  bool watch_file_dirty_bit_;
};

}  // namespace fakes
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_FAKES_CRASH_REPORTER_H_
