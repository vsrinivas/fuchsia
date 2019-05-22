// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/vsh/command_runner.h"

#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/vsh/client.h"

namespace vsh {

BlockingCommandRunner::BlockingCommandRunner(
    fidl::InterfaceHandle<fuchsia::virtualization::HostVsockEndpoint>
        socket_endpoint,
    uint32_t cid, uint32_t port)
    : socket_endpoint_(socket_endpoint.BindSync()), cid_(cid), port_(port) {}

fit::result<vsh::BlockingCommandRunner::CommandResult, zx_status_t>
BlockingCommandRunner::Execute(Command command) {
  auto client_result = BlockingClient::Connect(socket_endpoint_, cid_, port_);
  if (client_result.is_error()) {
    return fit::error(client_result.error());
  }
  auto client = client_result.take_value();

  vm_tools::vsh::SetupConnectionRequest setup_request;
  setup_request.set_nopty(true);
  setup_request.mutable_env()->insert(command.env.begin(), command.env.end());
  for (auto& arg : command.argv) {
    setup_request.add_argv(std::move(arg));
  }
  zx_status_t status = client.Setup(std::move(setup_request));
  if (status != ZX_OK) {
    return fit::error(status);
  }

  std::string out;
  std::string err;
  int32_t return_code;
  while (client.status() == vm_tools::vsh::ConnectionStatus::READY) {
    fit::result<vm_tools::vsh::HostMessage, zx_status_t> message_result =
        client.NextMessage();
    if (message_result.is_error()) {
      break;
    }
    auto message = message_result.take_value();
    switch (message.msg_case()) {
      case vm_tools::vsh::HostMessage::MsgCase::kStatusMessage: {
        auto new_status = message.status_message().status();
        switch (new_status) {
          case vm_tools::vsh::ConnectionStatus::EXITED: {
            return_code = message.status_message().code();
          } break;
          case vm_tools::vsh::ConnectionStatus::FAILED: {
            FXL_LOG(ERROR) << "Fatal error: " << message.status_message().description();
            return fit::error(ZX_ERR_CONNECTION_RESET);
          } break;
          default: {
            FXL_LOG(ERROR) << "Invalid state change to " << new_status;
            return fit::error(ZX_ERR_INVALID_ARGS);
          } break;
        }
      } break;
      case vm_tools::vsh::HostMessage::MsgCase::kDataMessage: {
        auto data = message.data_message();
        switch (data.stream()) {
          case vm_tools::vsh::StdioStream::STDOUT_STREAM: {
            out.append(data.data());
          } break;
          case vm_tools::vsh::StdioStream::STDERR_STREAM: {
            err.append(data.data());
          } break;
          default: {
            FXL_LOG(ERROR) << "Unsupported STDIO stream " << data.stream();
            return fit::error(ZX_ERR_NOT_SUPPORTED);
          } break;
        }
      } break;
      default: {
        FXL_LOG(ERROR) << "Unsupported message type " << message.msg_case();
        return fit::error(ZX_ERR_NOT_SUPPORTED);
      } break;
    }
  }

  return fit::ok(vsh::BlockingCommandRunner::CommandResult{
      out,
      err,
      return_code,
  });
}

}  // namespace vsh
