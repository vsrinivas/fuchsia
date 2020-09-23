// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_H_

#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "src/developer/forensics/crash_reports/snapshot_manager.h"
#include "src/developer/forensics/utils/sized_data.h"

namespace forensics {
namespace crash_reports {

// An in-memory representation of a report that will be uploaded to the crash server.
class Report {
 public:
  // Return a Report unless there are issues reading a fuchsia::mem::Buffer.
  static std::optional<Report> MakeReport(const std::string& program_shortname,
                                          const std::map<std::string, std::string>& annotations,
                                          std::map<std::string, fuchsia::mem::Buffer> attachments,
                                          forensics::crash_reports::SnapshotUuid snapshot_uuid,
                                          std::optional<fuchsia::mem::Buffer> minidump);

  Report(const std::string& program_shortname,
         const std::map<std::string, std::string>& annotations,
         std::map<std::string, SizedData> attachments, SnapshotUuid snapshot_uuid,
         std::optional<SizedData> minidump);

  std::string ProgramShortname() const { return program_shortname_; }
  const std::map<std::string, std::string>& Annotations() const { return annotations_; }
  const std::map<std::string, SizedData>& Attachments() const { return attachments_; }
  const std::optional<SizedData>& Minidump() const { return minidump_; }
  forensics::crash_reports::SnapshotUuid SnapshotUuid() const { return snapshot_uuid_; }

 private:
  std::string program_shortname_;
  std::map<std::string, std::string> annotations_;
  std::map<std::string, SizedData> attachments_;
  forensics::crash_reports::SnapshotUuid snapshot_uuid_;
  std::optional<SizedData> minidump_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_H_
