// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/feedback_agent.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/processargs.h>

#include <cinttypes>

#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

FeedbackAgent::FeedbackAgent(inspect::Node* node)
    : total_num_connections_(node, "total_num_connections", 0),
      current_num_connections_(node, "current_num_connections", 0) {}

void FeedbackAgent::SpawnNewDataProvider(
    fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
  total_num_connections_.Add(1);

  // We spawn a new process to which we forward the channel of the incoming request so it can
  // handle it.
  fdio_spawn_action_t actions = {};
  actions.action = FDIO_SPAWN_ACTION_ADD_HANDLE;
  actions.h.id = PA_HND(PA_USER0, 0);
  actions.h.handle = request.TakeChannel().release();

  const std::string process_name = "feedback_data_provider";
  const std::string connection_id = fxl::StringPrintf("%03" PRIu64, total_num_connections_.Get());
  const char* args[] = {
      process_name.c_str(),
      connection_id.c_str(),
      nullptr,
  };
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH] = {};
  zx_handle_t process;
  if (const zx_status_t status =
          fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, "/pkg/bin/data_provider", args,
                         nullptr, 1, &actions, &process, err_msg);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to spawn data provider to handle incoming request: "
                            << err_msg;
    return;
  }

  auto hook = std::make_unique<async::WaitMethod<FeedbackAgent, &FeedbackAgent::TaskTerminated>>(
      this, process, ZX_TASK_TERMINATED, ZX_WAIT_ASYNC_ONCE);
  on_process_exit_[process] = std::move(hook);
  on_process_exit_[process]->Begin(async_get_default_dispatcher());

  current_num_connections_.Add(1);
}

void FeedbackAgent::TaskTerminated(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                   zx_status_t status, const zx_packet_signal_t* signal) {
  current_num_connections_.Add(-1);
  zx_handle_t process = wait->object();
  if (auto entry = on_process_exit_.find(process); entry != on_process_exit_.end()) {
    on_process_exit_.erase(entry);
  }
}

FeedbackAgent::Counter::Counter(inspect::Node* parent, const std::string& name,
                                const uint64_t value)
    : value_(value) {
  metric_ = parent->CreateUint(std::move(name), value);
}

void FeedbackAgent::Counter::Add(int64_t delta) {
  value_ += delta;
  metric_.Add(delta);
}

uint64_t FeedbackAgent::Counter::Get() { return value_; }

}  // namespace feedback
