// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/server.h"

#include "lib/async-loop/default.h"
#include "lib/fidl-async/cpp/bind.h"
#include "lib/svc/dir.h"
#include "src/lib/fxl/logging.h"
#include "zircon/processargs.h"
#include "zircon/status.h"

namespace shell {
namespace interpreter {
namespace server {

void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto server = static_cast<Server*>(untyped_context);
  server->IncommingConnection(service_request);
}

void Service::CreateExecutionContext(uint64_t context_id,
                                     CreateExecutionContextCompleter::Sync completer) {
  auto context = contexts_.find(context_id);
  if (context != contexts_.end()) {
    OnError(0, "Execution context " + std::to_string(context_id) + " is already in use.");
  } else {
    contexts_.emplace(context_id, std::make_unique<ExecutionContext>(context_id));
  }
}

void Service::ExecuteExecutionContext(uint64_t context_id,
                                      ExecuteExecutionContextCompleter::Sync completer) {
  auto context = contexts_.find(context_id);
  if (context == contexts_.end()) {
    OnError(0, "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    // Currently, there is no way to define an instruction. That means that the server always
    // returns the same error (and does nothing).
    OnError(context_id, "No pending instruction to execute.");
    OnExecutionDone(context_id, fuchsia::shell::ExecuteResult::ANALYSIS_ERROR);
    contexts_.erase(context);
  }
}

Server::Server() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

bool Server::Listen() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "error: directory_request was ZX_HANDLE_INVALID";
    return false;
  }

  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(loop_.dispatcher(), directory_request, &dir);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error: svc_dir_create failed: " << status << " ("
                   << zx_status_get_string(status) << ")";
    return false;
  }

  status = svc_dir_add_service(dir, "svc", "fuchsia.shell.Shell", this, connect);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "error: svc_dir_add_service failed: " << status << " ("
                   << zx_status_get_string(status) << ")" << std::endl;
    return false;
  }
  return true;
}

void Server::IncommingConnection(zx_handle_t service_request) {
  fidl::Bind(loop_.dispatcher(), zx::channel(service_request), AddConnection(service_request));
}

}  // namespace server
}  // namespace interpreter
}  // namespace shell
