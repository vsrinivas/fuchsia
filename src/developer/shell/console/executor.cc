// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/executor.h"

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "src/developer/shell/common/result.h"

namespace shell::console {

Executor::Executor(llcpp::fuchsia::shell::Shell::SyncClient* client)
    : context_id_(0), client_(client) {}

Executor::~Executor() = default;

Err Executor::Execute(std::unique_ptr<Command> command,
                      fit::function<void(const std::string&)> out_callback,
                      fit::function<void(const std::string&)> err_callback,
                      fit::callback<void()> done_callback) {
  if (!command->parse_error().empty()) {
    err_callback("Parse:\n" + command->parse_error());
    return Err(ZX_ERR_NEXT, zx_status_get_string(ZX_ERR_NEXT));
  }
  if (command->nodes().empty()) {
    return Err(ZX_ERR_NEXT, zx_status_get_string(ZX_ERR_NEXT));
  }
  context_id_ += 1;
  llcpp::fuchsia::shell::Shell::ResultOf::CreateExecutionContext create_result =
      client_->CreateExecutionContext(context_id_);
  if (!create_result.ok()) {
    return Err(create_result.status(), create_result.error());
  }

  // TODO: Make sure that add_result is small enough to fit in a single FIDL message.  Otherwise,
  // split it.
  llcpp::fuchsia::shell::Shell::ResultOf::AddNodes add_result =
      client_->AddNodes(context_id_, command->nodes().DefsAsVectorView());
  if (!add_result.ok()) {
    return Err(add_result.status(), add_result.error());
  }

  llcpp::fuchsia::shell::Shell::ResultOf::ExecuteExecutionContext execute_result =
      client_->ExecuteExecutionContext(context_id_);
  if (!execute_result.ok()) {
    return Err(execute_result.status(), execute_result.error());
  }

  class EventHandler : public llcpp::fuchsia::shell::Shell::EventHandler {
   public:
    EventHandler(fit::function<void(const std::string&)>& out_callback,
                 fit::function<void(const std::string&)>& err_callback)
        : out_callback_(out_callback), err_callback_(err_callback) {}

    bool done() const { return done_; }

    void OnTextResult(llcpp::fuchsia::shell::Shell::OnTextResultResponse* event) override {
      out_callback_(event->result.data());
    }

    void OnDumpDone(llcpp::fuchsia::shell::Shell::OnDumpDoneResponse* event) override {}

    void OnExecutionDone(llcpp::fuchsia::shell::Shell::OnExecutionDoneResponse* event) override {
      done_ = true;
    }

    void OnError(llcpp::fuchsia::shell::Shell::OnErrorResponse* event) override {
      err_callback_(event->error_message.data());
    }

    void OnResult(llcpp::fuchsia::shell::Shell::OnResultResponse* event) override {
      if (event->partial_result) {
        err_callback_("Result too large: partial results not supported");
      } else {
        std::stringstream ss;
        shell::common::DeserializeResult deserialize;
        deserialize.Deserialize(event->nodes)->Dump(ss);
        out_callback_(ss.str());
      }
    }

    zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }

   private:
    fit::function<void(const std::string&)>& out_callback_;
    fit::function<void(const std::string&)>& err_callback_;
    bool done_ = false;
  };

  EventHandler event_handler(out_callback, err_callback);
  while (!event_handler.done()) {
    ::fidl::Result result = client_->HandleOneEvent(event_handler);
    if (!result.ok()) {
      return Err(result.status(), result.status_string());
    }
  }
  if (done_callback != nullptr) {
    done_callback();
  }

  return Err(ZX_ERR_NEXT, zx_status_get_string(ZX_ERR_NEXT));
}

void Executor::KillForegroundTask() {
  // TODO(fidl-tools-team): What happens when we hit ^C?
}

}  // namespace shell::console
