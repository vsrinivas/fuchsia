// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport_socket.h"

#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/internal.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <cstring>
#include <memory>

namespace fidl {
namespace internal {

namespace {

zx_status_t socket_write(fidl_handle_t handle, WriteOptions write_options, const WriteArgs& args) {
  ZX_ASSERT(args.handles_count == 0);
  ZX_ASSERT(args.data_count == 1);  // 1 iovec
  size_t actual;
  zx_channel_iovec_t iovec = static_cast<const zx_channel_iovec_t*>(args.data)[0];
  zx_status_t status = zx_socket_write(handle, 0, iovec.buffer, iovec.capacity, &actual);
  if (status != ZX_OK) {
    return status;
  }
  ZX_ASSERT(actual == iovec.capacity);
  return ZX_OK;
}

zx_status_t socket_read(fidl_handle_t handle, const ReadOptions& read_options,
                        const ReadArgs& args) {
  ZX_DEBUG_ASSERT(args.storage_view != nullptr);
  ZX_DEBUG_ASSERT(args.out_data != nullptr);
  SocketMessageStorageView* rd_view = static_cast<SocketMessageStorageView*>(args.storage_view);

  size_t actual;
  zx_status_t status =
      zx_socket_read(handle, 0, rd_view->bytes.data, rd_view->bytes.capacity, &actual);
  if (status != ZX_OK) {
    return status;
  }
  *args.out_data = rd_view->bytes.data;
  *args.out_data_actual_count = static_cast<uint32_t>(actual);
  *args.out_handles = nullptr;
  *args.out_handle_metadata = nullptr;
  *args.out_handles_actual_count = 0;
  return ZX_OK;
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
void socket_close_many(const fidl_handle_t* handles, size_t num_handles) {
  zx_handle_close_many(handles, num_handles);
}

}  // namespace

const TransportVTable SocketTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_TEST,
    .encoding_configuration = &SocketTransport::EncodingConfiguration,
    .write = socket_write,
    .read = socket_read,
    .create_waiter = socket_create_waiter,
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

  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT InlineMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES> bytes;
  IncomingHeaderAndMessage msg =
      fidl::MessageRead(zx::unowned_socket(async_wait_t::object),
                        fidl::internal::SocketMessageStorageView{.bytes = bytes.view()});
  if (!msg.ok()) {
    return failure_handler_(fidl::UnbindInfo{msg});
  }
  return success_handler_(msg, nullptr);
}

const CodingConfig SocketTransport::EncodingConfiguration = {
    .max_iovecs_write = 1,
    .handle_metadata_stride = 0,
    .close = socket_close,
    .close_many = socket_close_many,
};

}  // namespace internal
}  // namespace fidl
