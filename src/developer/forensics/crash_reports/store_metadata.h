// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_METADATA_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_METADATA_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "src/developer/forensics/crash_reports/report_id.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace crash_reports {

// In-memory metadata about the store in the filesystem at |store_root|.
//
// Note: Clients must use Add and Delete to keep the metadata in sync with the store in the
// filesystem. Use with caution!
class StoreMetadata {
 public:
  StoreMetadata(std::string store_root, StorageSize max_size);

  // Recreate the metadata from the store at |store_root_|.
  void RecreateFromFilesystem();

  bool Contains(ReportId report_id) const;
  bool Contains(const std::string& program) const;

  StorageSize CurrentSize() const;
  StorageSize RemainingSpace() const;

  void Add(ReportId report_id, std::string program, std::vector<std::string> attachments,
           StorageSize size);
  void Delete(ReportId report_id);

  std::vector<std::string> Programs() const;
  std::vector<ReportId> Reports() const;

  // The directory that contains reports for |program|.
  const std::string& ProgramDirectory(const std::string& program);

  // The ReportIds of all reports filed for |program|.
  const std::deque<ReportId>& ProgramReports(const std::string& program);

  // The program report |report_id| was filed under.
  const std::string& ReportProgram(ReportId report_id);

  // The directory that contains the attachments of report |report_id|.
  const std::string& ReportDirectory(ReportId report_id);

  // The size of report |report_id|.
  StorageSize ReportSize(ReportId report_id);

  // The attachments for report |report_id|. If |absolute_paths| is true, the absolute path of the
  // attachments in the filesystem will be returned otherwise the attachment file names will be
  // returned.
  std::vector<std::string> ReportAttachments(ReportId report_id, bool absolute_paths = false);

 private:
  // Metadata about each program including:
  //  1) The directory its reports are stored in.
  //  2) The reports filed for the program, in order from oldest to newest.
  struct ProgramMetadata {
    std::string dir;
    std::deque<ReportId> report_ids;
  };

  // Metadata about each report including:
  //  1) Its total size.
  //  2) The directory its attachments are stored in.
  //  3) The program it was filed under.
  //  4) The attachments it includes.
  struct ReportMetadata {
    StorageSize size;
    std::string dir;
    std::string program;
    std::vector<std::string> attachments;
  };

  // Where the store is located in the filesystem.
  std::string store_root_;

  StorageSize max_size_;
  StorageSize current_size_;

  std::map<std::string, ProgramMetadata> program_metadata_;
  std::map<ReportId, ReportMetadata> report_metadata_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_METADATA_H_
