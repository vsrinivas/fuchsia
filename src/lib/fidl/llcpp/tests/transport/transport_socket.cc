// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport_socket.h"

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <cstring>
#include <memory>

namespace fidl {
namespace internal {

namespace {

zx_status_t socket_write(fidl_handle_t handle, const WriteOptions& write_options, const void* data,
                         uint32_t data_count, const fidl_handle_t* handles,
                         const void* handle_metadata, uint32_t handles_count) {
  ZX_ASSERT(handles_count == 0);
  ZX_ASSERT(data_count == 1);  // 1 iovec
  size_t actual;
  zx_channel_iovec_t iovec = static_cast<const zx_channel_iovec_t*>(data)[0];
  zx_status_t status = zx_socket_write(handle, 0, iovec.buffer, iovec.capacity, &actual);
  if (status != ZX_OK) {
    return status;
  }
  ZX_ASSERT(actual == iovec.capacity);
  return ZX_OK;
}

void socket_read(fidl_handle_t handle, const ReadOptions& read_options,
                 TransportReadCallback callback) {
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  size_t actual;
  zx_status_t status = zx_socket_read(handle, 0, bytes, ZX_CHANNEL_MAX_MSG_BYTES, &actual);
  if (status != ZX_OK) {
    callback(Result::TransportError(status), nullptr, 0, nullptr, nullptr, 0,
             IncomingTransportContext());
    return;
  }

  callback(Result::Ok(), bytes, static_cast<uint32_t>(actual), nullptr, nullptr, 0,
           IncomingTransportContext());
}

zx_status_t socket_create_waiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                                 TransportWaitSuccessHandler success_handler,
                                 TransportWaitFailureHandler failure_handler,
                                 AnyTransportWaiter& any_transport_waiter) {
  any_transport_waiter.emplace<SocketWaiter>(handle, dispatcher, std::move(success_handler),
                                             std::move(failure_handler));
  return ZX_OK;
}

void socket_close(fidl_handle_t handle) { zx_handle_close(handle); }

}  // namespace

const TransportVTable SocketTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_TEST,
    .encoding_configuration = &SocketTransport::EncodingConfiguration,
    .write = socket_write,
    .read = socket_read,
    .create_waiter = socket_create_waiter,
    .close = socket_close,
};

void SocketWaiter::HandleWaitFinished(async_dispatcher_t* dispatcher, zx_status_t status,
                                      const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    return failure_handler_(fidl::UnbindInfo::DispatcherError(status));
  }
  if (!(signal->observed & ZX_SOCKET_READABLE)) {
    ZX_ASSERT(signal->observed & ZX_SOCKET_PEER_CLOSED);
    return failure_handler_(fidl::UnbindInfo::PeerClosed(ZX_ERR_PEER_CLOSED));
  }

  fidl::MessageRead(zx::unowned_socket(async_wait_t::object),
                    [this](IncomingMessage msg,
                           fidl::internal::IncomingTransportContext incoming_transport_context) {
                      if (!msg.ok()) {
                        return failure_handler_(fidl::UnbindInfo{msg});
                      }
                      return success_handler_(msg, fidl::internal::IncomingTransportContext());
                    });
}

const CodingConfig SocketTransport::EncodingConfiguration = {};

}  // namespace internal
}  // namespace fidl
