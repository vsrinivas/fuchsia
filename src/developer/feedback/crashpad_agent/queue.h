// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_H_

#include <map>
#include <vector>

#include "src/developer/feedback/crashpad_agent/crash_server.h"
#include "src/developer/feedback/crashpad_agent/database.h"
#include "src/developer/feedback/crashpad_agent/inspect_manager.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace feedback {

// Queues pending reports and processes them according to its internal State.
class Queue {
 public:
  struct Config {
    CrashpadDatabaseConfig database_config;

    // Maximum number of times to attempt to upload a report.
    uint64_t max_upload_attempts;
  };

  static std::unique_ptr<Queue> TryCreate(Config config, CrashServer* crash_server,
                                          InspectManager* inspect_manager);

  // Add a report to the queue.
  bool Add(std::map<std::string, fuchsia::mem::Buffer> atttachments,
           std::optional<fuchsia::mem::Buffer> minidump,
           std::map<std::string, std::string> annotations);

  // Process the pending reports based on the queue's internal state.
  void ProcessAll();

  uint64_t Size() const { return pending_reports_.size(); }
  bool IsEmpty() const { return pending_reports_.empty(); }
  bool Contains(const crashpad::UUID& uuid) const;
  const crashpad::UUID& LatestReport() { return pending_reports_.back(); }

  void SetStateToArchive() { state_ = State::Archive; }
  void SetStateToUpload() { state_ = State::Upload; }
  void SetStateToLeaveAsPending() { state_ = State::LeaveAsPending; }

 private:
  Queue(Config config, std::unique_ptr<Database> database, CrashServer* crash_server,
        InspectManager* inspect_manager);

  // How the queue should handle processing existing pending reports and new reports.
  enum class State {
    Archive,
    Upload,
    LeaveAsPending,
  };

  // Archives all pending reports and clears the queue.
  void ArchiveAll();

  // Attempts to upload all pending reports and removes the successfully uploaded reports from the
  // queue.
  void UploadAll();

  // Attempts to upload a report.
  //
  // Returns false if the report needs to be processed again.
  bool Upload(const crashpad::UUID& local_report_id);

  const Config config_;
  std::unique_ptr<Database> database_;
  CrashServer* crash_server_;
  InspectManager* inspect_manager_;

  State state_ = State::LeaveAsPending;

  std::vector<crashpad::UUID> pending_reports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Queue);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_H_
