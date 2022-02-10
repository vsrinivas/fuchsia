// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/trace.h>
#include <zircon/syscalls.h>

#include <cstring>

namespace fidl {
namespace internal {

namespace {

zx_status_t channel_write(fidl_handle_t handle, WriteOptions write_options, const void* data,
                          uint32_t data_count, const fidl_handle_t* handles,
                          const void* handle_metadata, uint32_t handles_count) {
  zx_handle_disposition_t hds[ZX_CHANNEL_MAX_MSG_HANDLES];
  const fidl_channel_handle_metadata_t* metadata =
      static_cast<const fidl_channel_handle_metadata_t*>(handle_metadata);
  for (uint32_t i = 0; i < handles_count; i++) {
    hds[i] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = handles[i],
        .type = metadata[i].obj_type,
        .rights = metadata[i].rights,
        .result = ZX_OK,
    };
  }
  return zx_channel_write_etc(handle, ZX_CHANNEL_WRITE_USE_IOVEC, data, data_count,
                              reinterpret_cast<zx_handle_disposition_t*>(hds), handles_count);
}

zx_status_t channel_read(fidl_handle_t handle, const ReadOptions& read_options, void* data,
                         uint32_t data_capacity, fidl_handle_t* handles, void* handle_metadata,
                         uint32_t handles_capacity, uint32_t* out_data_actual_count,
                         uint32_t* out_handles_actual_count) {
  uint32_t options = 0;
  if (read_options.discardable) {
    options |= ZX_CHANNEL_READ_MAY_DISCARD;
  }

  *out_data_actual_count = 0;
  *out_handles_actual_count = 0;
  zx_handle_info_t his[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_status_t status =
      zx_channel_read_etc(handle, options, data, his, data_capacity, handles_capacity,
                          out_data_actual_count, out_handles_actual_count);
  fidl_channel_handle_metadata_t* metadata =
      static_cast<fidl_channel_handle_metadata_t*>(handle_metadata);
  for (uint32_t i = 0; i < *out_handles_actual_count; i++) {
    handles[i] = his[i].handle;
    metadata[i] = fidl_channel_handle_metadata_t{
        .obj_type = his[i].type,
        .rights = his[i].rights,
    };
  }
  return status;
}

zx_status_t channel_call(fidl_handle_t handle, CallOptions call_options,
                         const CallMethodArgs& cargs, uint32_t* out_data_actual_count,
                         uint32_t* out_handles_actual_count) {
  ZX_DEBUG_ASSERT(cargs.out_rd_data == nullptr);
  ZX_DEBUG_ASSERT(cargs.rd_data != nullptr);

  zx_handle_disposition_t hds[ZX_CHANNEL_MAX_MSG_HANDLES];
  const fidl_channel_handle_metadata_t* wr_metadata =
      reinterpret_cast<const fidl_channel_handle_metadata_t*>(cargs.wr_handle_metadata);
  for (uint32_t i = 0; i < cargs.wr_handles_count; i++) {
    hds[i] = zx_handle_disposition_t{
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = cargs.wr_handles[i],
        .type = wr_metadata[i].obj_type,
        .rights = wr_metadata[i].rights,
        .result = ZX_OK,
    };
  }
  zx_handle_info_t his[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_channel_call_etc_args_t args = {
      .wr_bytes = cargs.wr_data,
      .wr_handles = hds,
      .rd_bytes = cargs.rd_data,
      .rd_handles = his,
      .wr_num_bytes = cargs.wr_data_count,
      .wr_num_handles = cargs.wr_handles_count,
      .rd_num_bytes = cargs.rd_data_capacity,
      .rd_num_handles = cargs.rd_handles_capacity,
  };
  zx_status_t status =
      zx_channel_call_etc(handle, ZX_CHANNEL_WRITE_USE_IOVEC, call_options.deadline, &args,
                          out_data_actual_count, out_handles_actual_count);
  fidl_channel_handle_metadata_t* rd_metadata =
      reinterpret_cast<fidl_channel_handle_metadata_t*>(cargs.rd_handle_metadata);
  for (uint32_t i = 0; i < *out_handles_actual_count; i++) {
    cargs.rd_handles[i] = his[i].handle;
    rd_metadata[i] = fidl_channel_handle_metadata_t{
        .obj_type = his[i].type,
        .rights = his[i].rights,
    };
  }
  return status;
}

zx_status_t channel_create_waiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                                  TransportWaitSuccessHandler success_handler,
                                  TransportWaitFailureHandler failure_handler,
                                  AnyTransportWaiter& any_transport_waiter) {
  any_transport_waiter.emplace<ChannelWaiter>(handle, dispatcher, std::move(success_handler),
                                              std::move(failure_handler));
  return ZX_OK;
}

void channel_close(fidl_handle_t handle) { zx_handle_close(handle); }

}  // namespace

const TransportVTable ChannelTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_CHANNEL,
    .encoding_configuration = &ChannelTransport::EncodingConfiguration,
    .write = channel_write,
    .read = channel_read,
    .call = channel_call,
    .create_waiter = channel_create_waiter,
    .close = channel_close,
};

void ChannelWaiter::HandleWaitFinished(async_dispatcher_t* dispatcher, zx_status_t status,
                                       const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    return failure_handler_(fidl::UnbindInfo::DispatcherError(status));
  }
  if (!(signal->observed & ZX_CHANNEL_READABLE)) {
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    return failure_handler_(fidl::UnbindInfo::PeerClosed(ZX_ERR_PEER_CLOSED));
  }

  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT InlineMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES> bytes;
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT fidl_channel_handle_metadata_t
      handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_trace(WillLLCPPAsyncChannelRead);
  IncomingMessage msg = fidl::MessageRead(zx::unowned_channel(async_wait_t::object), bytes.view(),
                                          handles, handle_metadata, ZX_CHANNEL_MAX_MSG_HANDLES);
  if (!msg.ok()) {
    return failure_handler_(fidl::UnbindInfo{msg});
  }
  fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes.data(), msg.byte_actual(),
             msg.handle_actual());
  return success_handler_(msg, IncomingTransportContext());
}

namespace {

zx_status_t channel_encode_process_handle(HandleAttributes attr, uint32_t metadata_index,
                                          void* out_metadata_array, const char** out_error) {
  reinterpret_cast<fidl_channel_handle_metadata_t*>(out_metadata_array)[metadata_index] = {
      .obj_type = attr.obj_type, .rights = attr.rights};
  return ZX_OK;
}
zx_status_t channel_decode_process_handle(fidl_handle_t* handle, HandleAttributes attr,
                                          uint32_t metadata_index, const void* metadata_array,
                                          const char** error) {
  fidl_channel_handle_metadata_t v =
      reinterpret_cast<const fidl_channel_handle_metadata_t*>(metadata_array)[metadata_index];
  return FidlEnsureHandleRights(handle, v.obj_type, v.rights, attr.obj_type, attr.rights, error);
}

}  // namespace

const CodingConfig ChannelTransport::EncodingConfiguration = {
    .max_iovecs_write = ZX_CHANNEL_MAX_MSG_IOVECS,
    .encode_process_handle = channel_encode_process_handle,
    .decode_process_handle = channel_decode_process_handle,
};

}  // namespace internal
}  // namespace fidl
