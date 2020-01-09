// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_DATABASE_INFO_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_DATABASE_INFO_H_

#include <memory>

#include "src/developer/feedback/crashpad_agent/info/info_context.h"

namespace feedback {

// Information about the database we want to export.
struct DatabaseInfo {
 public:
  DatabaseInfo(std::shared_ptr<InfoContext> context);

  void LogMaxCrashpadDatabaseSize(uint64_t max_crashpad_database_size_in_kb);
  void LogGarbageCollection(uint64_t num_cleaned, uint64_t num_pruned);

  void RecordUploadAttemptNumber(const std::string& local_report_id, uint64_t upload_attempt);

  void MarkReportAsUploaded(const std::string& local_report_id, const std::string& server_report_id,
                            uint64_t upload_attempts);
  void MarkReportAsArchived(const std::string& local_report_id, uint64_t upload_attempts);
  void MarkReportAsGarbageCollected(const std::string& local_report_id, uint64_t upload_attempts);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INFO_DATABASE_INFO_H_
