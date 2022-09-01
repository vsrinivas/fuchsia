// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/vsh/client.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/socket.h>
#include <zircon/status.h>

#include "src/virtualization/lib/vsh/util.h"

namespace vsh {

using ::fuchsia::virtualization::HostVsockEndpoint_Connect_Result;

// static
fpromise::result<BlockingClient, zx_status_t> BlockingClient::Connect(
    const fuchsia::virtualization::HostVsockEndpointSyncPtr& socket_endpoint, uint32_t port) {
  HostVsockEndpoint_Connect_Result result;
  zx_status_t fidl_status = socket_endpoint->Connect(port, &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Failed to connect to vshd: " << zx_status_get_string(result.err());
    return fpromise::error(result.err());
  }

  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error connecting to vshd: " << zx_status_get_string(fidl_status);
    return fpromise::error(fidl_status);
  }

  return fpromise::ok(BlockingClient(std::move(result.response().socket)));
}

BlockingClient::BlockingClient(zx::socket socket) : vsock_(std::move(socket)) {}

zx_status_t BlockingClient::Setup(vm_tools::vsh::SetupConnectionRequest conn_req) {
  vm_tools::vsh::SetupConnectionResponse conn_resp;

  if (!SendMessage(vsock_, conn_req)) {
    FX_LOGS(ERROR) << "Failed to send connection request";
    return ZX_ERR_INTERNAL;
  }

  if (!RecvMessage(vsock_, &conn_resp)) {
    FX_LOGS(ERROR) << "Failed to receive response from vshd, giving up after one try";
    return ZX_ERR_INTERNAL;
  }

  if (conn_resp.status() != vm_tools::vsh::READY) {
    FX_LOGS(ERROR) << "Server was unable to set up connection properly: "
                   << conn_resp.description();
    return ZX_ERR_INTERNAL;
  }

  status_ = conn_resp.status();
  return ZX_OK;
}

fpromise::result<vm_tools::vsh::HostMessage, zx_status_t> BlockingClient::NextMessage() {
  FX_CHECK(status_ == vm_tools::vsh::ConnectionStatus::READY);
  vm_tools::vsh::HostMessage msg;

  if (!RecvMessage(vsock_, &msg)) {
    FX_LOGS(ERROR) << "Failed to receive response from vshd, giving up after one try";
    return fpromise::error(ZX_ERR_INTERNAL);
  }
  if (msg.msg_case() == vm_tools::vsh::HostMessage::MsgCase::kStatusMessage) {
    auto new_status = msg.status_message().status();
    status_ = new_status;
  }
  return fpromise::ok(std::move(msg));
}

}  // namespace vsh
