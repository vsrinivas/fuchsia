// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback/annotation_providers.h"
#include "src/developer/forensics/feedback/crash_reports.h"
#include "src/developer/forensics/feedback/feedback_data.h"
#include "src/developer/forensics/feedback/last_reboot.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"
#include "src/developer/forensics/utils/redact/redactor.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics::feedback {

// Handles queueing connections to Feedback protocols while data migration is occurring and
// dispatching those requests once migration is complete.
class MainService {
 public:
  struct Options {
    std::optional<std::string> local_device_id_path;
    LastReboot::Options last_reboot_options;
    CrashReports::Options crash_reports_options;
    FeedbackData::Options feedback_data_options;
  };

  MainService(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
              timekeeper::Clock* clock, inspect::Node* inspect_root, cobalt::Logger* cobalt,
              const Annotations& startup_annotations, Options options);

  template <typename Protocol>
  ::fidl::InterfaceRequestHandler<Protocol> GetHandler();

  void ShutdownImminent(::fit::deferred_callback stop_respond);

  // Files a crash report indicating the Feedback migration experienced an error with the specified
  // annotations.
  void ReportMigrationError(const std::map<std::string, std::string>& annotations);

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;
  inspect::Node* inspect_root_;
  cobalt::Logger* cobalt_;
  std::unique_ptr<RedactorBase> redactor_;

  InspectNodeManager inspect_node_manager_;

  AnnotationProviders annotations_;

  FeedbackData feedback_data_;
  CrashReports crash_reports_;
  LastReboot last_reboot_;

  InspectProtocolStats last_reboot_info_provider_stats_;
  InspectProtocolStats crash_reporter_stats_;
  InspectProtocolStats crash_reporting_product_register_stats_;
  InspectProtocolStats component_data_register_stats_;
  InspectProtocolStats data_provider_stats_;
  InspectProtocolStats data_provider_controller_stats_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MAIN_SERVICE_H_
