// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <stdint.h>

#include <string>
#include <utility>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/crash_server.h"
#include "src/developer/feedback/crashpad_agent/inspect_manager.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace feedback {

class CrashpadAgent : public fuchsia::crash::Analyzer, public fuchsia::feedback::CrashReporter {
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

  // |fuchsia::crash::Analyzer|
  //
  // TODO(DX-1820): delete once transitioned to fuchsia.feedback.CrashReporter.
  void OnNativeException(zx::process process, zx::thread thread,
                         OnNativeExceptionCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }
  void OnManagedRuntimeException(std::string component_url,
                                 fuchsia::crash::ManagedRuntimeException exception,
                                 OnManagedRuntimeExceptionCallback callback) override;

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

 private:
  CrashpadAgent(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                Config config, std::unique_ptr<crashpad::CrashReportDatabase> database,
                std::unique_ptr<CrashServer> crash_server, InspectManager* inspect_manager);

  fit::promise<void> OnManagedRuntimeException(std::string component_url,
                                               fuchsia::crash::ManagedRuntimeException exception);
  fit::promise<void> File(fuchsia::feedback::CrashReport report);

  // Uploads local crash report of ID |local_report_id|, attaching the passed |annotations|.
  bool UploadReport(const crashpad::UUID& local_report_id, const std::string& program_name,
                    const std::map<std::string, std::string>& annotations);

  // Deletes oldest crash reports to keep |database_| under a maximum size read from |config_|.
  //
  // Report age is defined by their crashpad::CrashReportDatabase::Report::creation_time.
  void PruneDatabase();

  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const Config config_;
  const std::unique_ptr<crashpad::CrashReportDatabase> database_;
  const std::unique_ptr<CrashServer> crash_server_;
  InspectManager* inspect_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
