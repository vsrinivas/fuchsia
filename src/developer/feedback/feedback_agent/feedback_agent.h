// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include <cstdint>

#include "src/developer/feedback/feedback_agent/inspect_manager.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Launches and manages the lifetime of |data_provider| processes, keeping global Inspect state up
// to date.
class FeedbackAgent {
 public:
  FeedbackAgent(inspect::Node* root_node);

  void SpawnSystemLogRecorder();
  void SpawnNewDataProvider(fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request);

 private:
  void TaskTerminated(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal);

  InspectManager inspect_manager_;

  uint64_t next_data_provider_connection_id_ = 1;

  // Maps each subprocess to what to do when it exits.
  std::map<zx_handle_t,
           std::unique_ptr<async::WaitMethod<FeedbackAgent, &FeedbackAgent::TaskTerminated>>>
      on_process_exit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
