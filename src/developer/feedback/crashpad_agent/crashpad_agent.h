// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/crash_reporter.h"
#include "src/developer/feedback/crashpad_agent/info/agent_info.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {

// Main class that handles incoming CrashReporter requests, manages the component's Inspect state,
// etc.
class CrashpadAgent {
 public:
  // Static factory methods.
  //
  // Returns nullptr if the agent cannot be instantiated, e.g., because the config cannot be parsed
  // or the crash reporter instantiated.
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  const timekeeper::Clock& clock,
                                                  std::shared_ptr<InfoContext> info_context);
  static std::unique_ptr<CrashpadAgent> TryCreate(async_dispatcher_t* dispatcher,
                                                  std::shared_ptr<sys::ServiceDirectory> services,
                                                  const timekeeper::Clock& clock,
                                                  std::shared_ptr<InfoContext> info_context,
                                                  Config config);

  // FIDL protocol handlers.
  //
  // fuchsia.feedback.CrashReporter
  void HandleCrashReporterRequest(fidl::InterfaceRequest<fuchsia::feedback::CrashReporter> request);

 private:
  CrashpadAgent(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                std::shared_ptr<InfoContext> info_context, Config config,
                std::unique_ptr<CrashReporter> crash_reporter);

  async_dispatcher_t* dispatcher_;
  AgentInfo info_;
  const Config config_;

  std::unique_ptr<CrashReporter> crash_reporter_;
  fidl::BindingSet<fuchsia::feedback::CrashReporter> crash_reporter_connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CrashpadAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_CRASHPAD_AGENT_H_
