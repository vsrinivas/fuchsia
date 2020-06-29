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

namespace forensics {
namespace crash_reports {

// An in-memory representation of a report that will be uploaded to the crash server.
class Report {
 public:
  // We copy the VMO held by the kernel into memory owned by the process to limit then number
  // context switches when uploading crash reports.
  struct Attachment {
    // Attachments can be large (on the order of megabytes) so uninteded copies need to be
    // prevented.
    std::unique_ptr<uint8_t[]> data;
    size_t size;
  };

  // Return a Report unless there are issues reading a fuchsia::mem::Buffer.
  static std::optional<Report> MakeReport(const std::string& program_shortname,
                                          const std::map<std::string, std::string>& annotations,
                                          std::map<std::string, fuchsia::mem::Buffer> attachments,
                                          std::optional<fuchsia::mem::Buffer> minidump);

  const std::map<std::string, std::string>& Annotations() const { return annotations_; }
  const std::map<std::string, Attachment>& Attachments() const { return attachments_; }
  const std::optional<Attachment>& Minidump() const { return minidump_; }

 private:
  Report(const std::string& program_shortname,
         const std::map<std::string, std::string>& annotations,
         std::map<std::string, Attachment> attachments, std::optional<Attachment> minidump);

  std::string program_shortname_;
  std::map<std::string, std::string> annotations_;
  std::map<std::string, Attachment> attachments_;
  std::optional<Attachment> minidump_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_REPORT_H_
