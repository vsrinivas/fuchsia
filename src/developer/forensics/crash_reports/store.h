// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_H_

#include <deque>
#include <map>
#include <optional>
#include <string>

#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/info/store_info.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics {
namespace crash_reports {

// Stores the contents of reports that have not yet been uploaded.
class Store {
 public:
  // A unique report identifier.
  using Uid = uint64_t;

  // |root_dir| is the location in the filesystem where reports will be stored. For example,
  // if |root_dir| is /tmp/store and a report for "foo" is filed, that report
  // will be stored in /tmp/store/foo/<report Uid>.
  // |max_size| is the maximum size the store can take, garbage collecting the reports of lowest
  // Uids.
  Store(std::shared_ptr<InfoContext> info_context, const std::string& root_dir,
        StorageSize max_size);

  // Adds a report to the store and returns the Uids of any report garbage collected in the process.
  // If the operation fails, std::nullopt is returned, else a unique identifier referring to the
  // report is returned.
  std::optional<Uid> Add(Report report, std::vector<Uid>* garbage_collected_reports);

  // Gets a report from the store. If no report exists for |id| or there is an error reading the
  // report from the filesystem, return std::nullopt.
  std::optional<Report> Get(const Uid& id);

  // Returns true if a report with Uid |id| is removed from the store.
  bool Remove(const Uid& id);

  void RemoveAll();

  std::vector<Uid> GetAllUids() const;

  // Exposed for testing purposes.
  bool Contains(const Uid& id) const;

 private:
  // Rebuilds the non-persistent metadata about the store, e.g. |ids_to_metadata_|, from the
  // store present under |root_dir_|.
  void RebuildMetadata();

  // Removes reports until |required_space| is free in the store and returns the Uids of the reports
  // removed.
  //
  // Return false if |required_space| cannot be freed.
  bool MakeFreeSpace(StorageSize required_space, std::vector<Uid>* garbage_collected_reports);

  struct ReportMetadata {
    // The directory containing the report's files, e.g., /tmp/crashes/foo/<report Uid>
    std::string dir;

    // The total size taken by the report's files.
    StorageSize size;

    // "foo" in the above example, it shouldn't contain forward slashes.
    std::string program_shortname;
  };

  std::string root_dir_;

  StorageSize max_size_;
  StorageSize current_size_;

  std::map<Uid, ReportMetadata> id_to_metadata_;

  // The uids for a given program shortname. The uids are stored in the order they're generated to
  // make garbage collection easy.
  std::map<std::string, std::deque<Uid>> reports_for_program_;
  Uid next_id_{0};

  StoreInfo info_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_STORE_H_
