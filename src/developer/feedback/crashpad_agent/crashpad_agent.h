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
#include "src/developer/feedback/crashpad_agent/info/agent_info.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/developer/feedback/crashpad_agent/privacy_settings_ptr.h"
#include "src/developer/feedback/crashpad_agent/queue.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/developer/feedback/utils/utc_time_provider.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {

class CrashpadAgent : public fuchsia::feedback::CrashReporter {
 public:
  // Static factory methods.
  //
  // Returns nullptr if the agent cannot be instantiated, e.g., because the local report database
  // cannot be accessed.
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  const timekeeper::Clock& clock,
                                                  std::shared_ptr<InfoContext> info_context);
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  const timekeeper::Clock& clock,
                                                  std::shared_ptr<InfoContext> info_context,
                                                  Config config);
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  const timekeeper::Clock& clock,
                                                  std::shared_ptr<InfoContext> info_context,
                                                  Config config,
                                                  std::unique_ptr<CrashServer> crash_server);

  // |fuchsia::feedback::CrashReporter|
  void File(fuchsia::feedback::CrashReport report, FileCallback callback) override;

 private:
  CrashpadAgent(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                const timekeeper::Clock& clock, std::shared_ptr<InfoContext> info_context,
                Config config, std::unique_ptr<CrashServer> crash_server,
                std::unique_ptr<Queue> queue);

  async_dispatcher_t* dispatcher_;
  async::Executor executor_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  const Config config_;
  const UTCTimeProvider utc_provider_;
  const std::unique_ptr<Queue> queue_;
  const std::unique_ptr<CrashServer> crash_server_;
  AgentInfo info_;
  Settings settings_;
  PrivacySettingsWatcher privacy_settings_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
