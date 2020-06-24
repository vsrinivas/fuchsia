// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/upload_report.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

using crashpad::CrashReportDatabase;
using crashpad::FileReader;
using crashpad::UUID;

UploadReport::UploadReport(std::unique_ptr<const CrashReportDatabase::UploadReport> upload_report,
                           const std::map<std::string, std::string>& annotations, bool has_minidump)
    : upload_report_(std::move(upload_report)),
      annotations_(std::move(annotations)),
      has_minidump_(has_minidump) {}

std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport>
UploadReport::TransferUploadReport() {
  FX_CHECK(upload_report_);
  return std::move(upload_report_);
}

std::map<std::string, std::string> UploadReport::GetAnnotations() const {
  FX_CHECK(upload_report_);
  return annotations_;
}

std::map<std::string, FileReader*> UploadReport::GetAttachments() const {
  FX_CHECK(upload_report_);
  auto attachments = upload_report_->GetAttachments();
  if (has_minidump_) {
    attachments["uploadFileMinidump"] = upload_report_->Reader();
  }
  return attachments;
}

UUID UploadReport::GetUUID() const {
  FX_CHECK(upload_report_);
  return upload_report_->uuid;
}

}  // namespace crash_reports
}  // namespace forensics
