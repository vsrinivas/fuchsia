// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/util/error_reporter.h"

namespace scene_manager {

namespace {

#define LOG_HELPER(severity) FXL_LOG(severity) << error_string;

class DefaultErrorReporter : public ErrorReporter {
 public:
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override {
    switch (severity) {
      case fxl::LOG_INFO:
        FXL_LOG(INFO) << error_string;
        break;
      case fxl::LOG_WARNING:
        FXL_LOG(WARNING) << error_string;
        break;
      case fxl::LOG_ERROR:
        FXL_LOG(ERROR) << error_string;
        break;
      case fxl::LOG_FATAL:
        FXL_LOG(FATAL) << error_string;
        break;
      default:
        // Invalid severity.
        FXL_DCHECK(false);
    }
  }
};

}  // anonymous namespace

ErrorReporter::Report::Report(ErrorReporter* owner, fxl::LogSeverity severity)
    : owner_(owner), severity_(severity) {}

ErrorReporter::Report::Report(Report&& other)
    : owner_(other.owner_),
      severity_(other.severity_),
      stream_(std::move(other.stream_)) {}

ErrorReporter::Report::~Report() {
  owner_->ReportError(severity_, stream_.str());
}

ErrorReporter* ErrorReporter::Default() {
  static DefaultErrorReporter reporter;
  return &reporter;
}

}  // namespace scene_manager
