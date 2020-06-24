// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_UPLOAD_REPORT_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_UPLOAD_REPORT_H_

#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/client/crash_report_database.h"

namespace forensics {
namespace crash_reports {

// Wrapper around Crashpad's UploadReport that also stores the annotations.
//
// Note: Destryoing an instance of this class will increment the number of upload attempts
// for the undelying report in the Crashpad database.
class UploadReport {
 public:
  UploadReport(std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> upload_report,
               const std::map<std::string, std::string>& annotations, bool has_minidump);

  ~UploadReport() = default;

  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> TransferUploadReport();
  std::map<std::string, std::string> GetAnnotations() const;
  std::map<std::string, crashpad::FileReader*> GetAttachments() const;
  crashpad::UUID GetUUID() const;

 private:
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> upload_report_;
  std::map<std::string, std::string> annotations_;
  bool has_minidump_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UploadReport);
};

}  // namespace crash_reports
}  // namespace forensics
#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_UPLOAD_REPORT_H_
