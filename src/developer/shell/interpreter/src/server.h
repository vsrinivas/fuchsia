// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SERVER_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SERVER_H_

#include <memory>
#include <vector>

#include "fuchsia/shell/cpp/fidl.h"
#include "fuchsia/shell/llcpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "zircon/status.h"

namespace shell {
namespace interpreter {
namespace server {

class Service;

// Defines an interpreter managed by a server.
class ServerInterpreter : public Interpreter {
 public:
  explicit ServerInterpreter(Service* service) : service_(service) {}

  void EmitError(ExecutionContext* context, std::string error_message) override;
  void ContextDoneWithAnalysisError(ExecutionContext* context) override;

 private:
  Service* service_;
};

// Defines a connection from a client to the interpreter.
class Service final : public llcpp::fuchsia::shell::Shell::Interface {
 public:
  explicit Service(zx_handle_t handle)
      : handle_(handle), interpreter_(std::make_unique<ServerInterpreter>(this)) {}

  void CreateExecutionContext(uint64_t context_id,
                              CreateExecutionContextCompleter::Sync completer) override;
  void ExecuteExecutionContext(uint64_t context_id,
                               ExecuteExecutionContextCompleter::Sync completer) override;

  // Helpers to be able to send events to the client.
  zx_status_t OnError(uint64_t context_id, std::vector<fuchsia::shell::Location>& locations,
                      std::string error_message) {
    fidl::Encoder encoder(fuchsia::shell::internal::kShell_OnError_GenOrdinal);
    auto message = fuchsia::shell::Shell_ResponseEncoder::OnError(&encoder, &context_id, &locations,
                                                                  &error_message);
    return message.Write(handle_, 0);
  }

  zx_status_t OnError(uint64_t context_id, std::string error_message) {
    std::vector<fuchsia::shell::Location> locations;
    return OnError(context_id, locations, error_message);
  }

  zx_status_t OnExecutionDone(uint64_t context_id, fuchsia::shell::ExecuteResult result) {
    fidl::Encoder encoder(fuchsia::shell::internal::kShell_OnExecutionDone_GenOrdinal);
    auto message =
        fuchsia::shell::Shell_ResponseEncoder::OnExecutionDone(&encoder, &context_id, &result);
    return message.Write(handle_, 0);
  }

 private:
  zx_handle_t handle_;
  std::unique_ptr<ServerInterpreter> interpreter_;
};

// Class which accept connections from clients. Each time a new connection is accepted, a Service
// object is created.
class Server {
 public:
  Server();

  Service* AddConnection(zx_handle_t handle) {
    auto service = std::make_unique<Service>(handle);
    auto result = service.get();
    services_.emplace_back(std::move(service));
    return result;
  }

  bool Listen();
  void IncommingConnection(zx_handle_t service_request);
  void Run() { loop_.Run(); }

 private:
  async::Loop loop_;
  std::vector<std::unique_ptr<Service>> services_;
};

}  // namespace server
}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SERVER_H_
