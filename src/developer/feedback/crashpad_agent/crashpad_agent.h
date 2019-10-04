// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/crash_server.h"
#include "src/developer/feedback/crashpad_agent/inspect_manager.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace feedback {

class CrashpadAgent : public fuchsia::feedback::CrashReporter {
 public:
  // Static factory methods.
  //
  // Returns nullptr if the agent cannot be instantiated, e.g., because the local report database
  // cannot be accessed.
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  InspectManager* inspect_manager);
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  Config config, InspectManager* inspect_manager);
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  Config config,
                                                  std::unique_ptr<CrashServer> crash_server,
                                                  InspectManager* inspect_manager);

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

 private:
  CrashpadAgent(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                Config config, std::unique_ptr<crashpad::CrashReportDatabase> database,
                std::unique_ptr<CrashServer> crash_server, InspectManager* inspect_manager);

  // Uploads local crash report of ID |local_report_id|, attaching the passed |annotations|.
  bool UploadReport(const crashpad::UUID& local_report_id,
                    const std::map<std::string, std::string>& annotations, bool has_minidump);

  // Deletes oldest crash reports to keep |database_| under a maximum size read from |config_|,
  // returning the number of pruned reports.
  //
  // Report age is defined by their crashpad::CrashReportDatabase::Report::creation_time.
  size_t PruneDatabase();

  // Removes expired lockfiles, metadata without report files, report files without
  // metadata from the database, and orphaned attachments.
  //
  // An expired lockfile is defined as having been alive longer than |lockfile_ttl|
  // seconds.
  //
  // Returns the number of reports cleaned.
  size_t CleanDatabase();

  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const Config config_;
  Settings settings_;
  const std::unique_ptr<crashpad::CrashReportDatabase> database_;
  const std::unique_ptr<CrashServer> crash_server_;
  InspectManager* inspect_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
