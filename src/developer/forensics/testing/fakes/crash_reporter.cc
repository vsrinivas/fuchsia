// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/fakes/crash_reporter.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <zircon/errors.h>

namespace forensics {
namespace fakes {

using namespace fuchsia::feedback;

void CrashReporter::File(CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    callback(CrashReporter_File_Result::WithErr(ZX_ERR_INVALID_ARGS));
  } else {
    callback(CrashReporter_File_Result::WithResponse(CrashReporter_File_Response()));
  }

  ++num_crash_reports_filed_;
  for (auto& querier : queriers_) {
    querier.UpdateAndNotify(num_crash_reports_filed_);
  }
}

void CrashReporter::AddNewQuerier(
    fidl::InterfaceRequest<fuchsia::feedback::testing::FakeCrashReporterQuerier> request) {
  queriers_.emplace_back(std::move(request), num_crash_reports_filed_);
}

FakeCrashReporterQuerier::FakeCrashReporterQuerier(
    fidl::InterfaceRequest<fuchsia::feedback::testing::FakeCrashReporterQuerier> request,
    size_t num_crash_reports_filed)
    : connection_(this, std::move(request)),
      num_crash_reports_filed_(num_crash_reports_filed),
      watch_file_dirty_bit_(true) {}

void FakeCrashReporterQuerier::UpdateAndNotify(size_t num_crash_reports_filed) {
  num_crash_reports_filed_ = num_crash_reports_filed;
  watch_file_dirty_bit_ = true;
  Notify();
}

void FakeCrashReporterQuerier::Notify() {
  if (!callback_.has_value() || !watch_file_dirty_bit_) {
    return;
  }

  (*callback_)(num_crash_reports_filed_);
  callback_ = std::nullopt;
  watch_file_dirty_bit_ = false;
}

void FakeCrashReporterQuerier::WatchFile(WatchFileCallback callback) {
  callback_ = std::move(callback);
  Notify();
}

}  // namespace fakes
}  // namespace forensics
