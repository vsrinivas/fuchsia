// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/vsh/client.h"

#include <lib/zx/socket.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/vsh/util.h"

namespace vsh {

// static
fit::result<BlockingClient, zx_status_t> BlockingClient::Connect(
    const fuchsia::virtualization::HostVsockEndpointSyncPtr& socket_endpoint,
    uint32_t cid, uint32_t port) {
  // Open a socket to the guest's vsock port where vshd should be listening
  zx::socket socket, remote_socket;
  zx_status_t status =
      zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: "
                   << zx_status_get_string(status);
    return fit::error(status);
  }
  zx_status_t fidl_status =
      socket_endpoint->Connect(cid, port, std::move(remote_socket), &status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect to vshd: "
                   << zx_status_get_string(status);
    return fit::error(status);
  }
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "FIDL error connecting to vshd: "
                   << zx_status_get_string(fidl_status);
    return fit::error(fidl_status);
  }
  return fit::ok(BlockingClient(std::move(socket)));
}

BlockingClient::BlockingClient(zx::socket socket) : vsock_(std::move(socket)) {}

zx_status_t BlockingClient::Setup(
    vm_tools::vsh::SetupConnectionRequest conn_req) {
  vm_tools::vsh::SetupConnectionResponse conn_resp;

  if (!SendMessage(vsock_, conn_req)) {
    FXL_LOG(ERROR) << "Failed to send connection request";
    return ZX_ERR_INTERNAL;
  }

  if (!RecvMessage(vsock_, &conn_resp)) {
    FXL_LOG(ERROR)
        << "Failed to receive response from vshd, giving up after one try";
    return ZX_ERR_INTERNAL;
  }

  if (conn_resp.status() != vm_tools::vsh::READY) {
    FXL_LOG(ERROR) << "Server was unable to set up connection properly: "
                   << conn_resp.description();
    return ZX_ERR_INTERNAL;
  }

  status_ = conn_resp.status();
  return ZX_OK;
}

fit::result<vm_tools::vsh::HostMessage, zx_status_t>
BlockingClient::NextMessage() {
  FXL_CHECK(status_ == vm_tools::vsh::ConnectionStatus::READY);
  vm_tools::vsh::HostMessage msg;

  if (!RecvMessage(vsock_, &msg)) {
    FXL_LOG(ERROR)
        << "Failed to receive response from vshd, giving up after one try";
    return fit::error(ZX_ERR_INTERNAL);
  }
  if (msg.msg_case() == vm_tools::vsh::HostMessage::MsgCase::kStatusMessage) {
    auto new_status = msg.status_message().status();
    status_ = new_status;
  }
  return fit::ok(std::move(msg));
}

}  // namespace vsh
