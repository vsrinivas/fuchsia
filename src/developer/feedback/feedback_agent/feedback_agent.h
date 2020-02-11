// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/sys/inspect/cpp/component.h>

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
  class Counter {
   public:
    Counter(inspect::Node* parent, const std::string& name, uint64_t value);

    void Add(int64_t delta);
    uint64_t Get();

   private:
    uint64_t value_;
    inspect::UintProperty metric_;
  };

  void TaskTerminated(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal);

  // Maps each subprocess to what to do when it exits.
  std::map<zx_handle_t,
           std::unique_ptr<async::WaitMethod<FeedbackAgent, &FeedbackAgent::TaskTerminated>>>
      on_process_exit_;

  // Inspect data.
  Counter total_num_connections_;
  Counter current_num_connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FeedbackAgent);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_FEEDBACK_AGENT_H_
