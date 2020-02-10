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

void ServerInterpreter::EmitError(ExecutionContext* context, std::string error_message) {
  service_->OnError((context == nullptr) ? 0 : context->id(), std::move(error_message));
}

void ServerInterpreter::ContextDoneWithAnalysisError(ExecutionContext* context) {
  FXL_DCHECK(context != nullptr);
  service_->OnExecutionDone(context->id(), fuchsia::shell::ExecuteResult::ANALYSIS_ERROR);
}

void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto server = static_cast<Server*>(untyped_context);
  server->IncommingConnection(service_request);
}

void Service::CreateExecutionContext(uint64_t context_id,
                                     CreateExecutionContextCompleter::Sync completer) {
  interpreter_->AddContext(context_id);
}

void Service::ExecuteExecutionContext(uint64_t context_id,
                                      ExecuteExecutionContextCompleter::Sync completer) {
  interpreter_->ExecuteContext(context_id);
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
