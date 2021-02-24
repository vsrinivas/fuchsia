// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_CONTROLLER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_CONTROLLER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/zx/channel.h>

namespace forensics {
namespace feedback_data {

class DataProviderController : public fuchsia::feedback::DataProviderController {
 public:
  void BindSystemLogRecorderController(zx::channel channel, async_dispatcher_t* dispatcher);

  // |fuchsia.feedback.DataProviderController|
  void DisableAndDropPersistentLogs(DisableAndDropPersistentLogsCallback callback) override;

 private:
  ::fidl::InterfacePtr<fuchsia::feedback::DataProviderController> system_log_recorder_controller_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_DATA_PROVIDER_CONTROLLER_H_
