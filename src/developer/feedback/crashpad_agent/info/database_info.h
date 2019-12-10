// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_DATABASE_INFO_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_DATABASE_INFO_H_

#include <memory>

#include "src/developer/feedback/crashpad_agent/info/info_context.h"

namespace feedback {

// Information about the database we want to export.
struct DatabaseInfo {
 public:
  DatabaseInfo(std::shared_ptr<InfoContext> context);

  void LogMaxCrashpadDatabaseSize(uint64_t max_crashpad_database_size_in_kb);
  void MarkReportAsUploaded(const std::string& local_report_id,
                            const std::string& server_report_id);
  void MarkReportAsArchived(const std::string& local_report_id);
  void MarkReportAsGarbageCollected(const std::string& local_report_id);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace feedback

#endif  //  SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_DATABASE_INFO_H_
