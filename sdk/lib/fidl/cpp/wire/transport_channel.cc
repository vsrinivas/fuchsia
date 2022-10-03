// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/synchronization_checker.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <zircon/syscalls.h>

#include <cstring>

namespace fidl {
namespace internal {

namespace {

class HandleDisposition {
 public:
  explicit HandleDisposition(const WriteArgs& args) {
    const fidl_channel_handle_metadata_t* metadata =
        reinterpret_cast<const fidl_channel_handle_metadata_t*>(args.handle_metadata);
    for (uint32_t i = 0; i < args.handles_count; i++) {
      hds_[i] = zx_handle_disposition_t{
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = args.handles[i],
          .type = metadata[i].obj_type,
          .rights = metadata[i].rights,
          .result = ZX_OK,
      };
    }
  }

  zx_handle_disposition_t* get() { return hds_; }

 private:
  zx_handle_disposition_t hds_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

class HandleInfo {
 public:
  void Read(const ReadArgs& args) {
    auto* rd_metadata =
        reinterpret_cast<fidl_channel_handle_metadata_t*>(*args.out_handle_metadata);
    zx_handle_t* rd_handles = *args.out_handles;
    for (uint32_t i = 0; i < *args.out_handles_actual_count; i++) {
      rd_handles[i] = his_[i].handle;
      rd_metadata[i] = fidl_channel_handle_metadata_t{
          .obj_type = his_[i].type,
          .rights = his_[i].rights,
      };
    }
  }

  zx_handle_info_t* get() { return his_; }

 private:
  zx_handle_info_t his_[ZX_CHANNEL_MAX_MSG_HANDLES];
};

zx_status_t channel_write(fidl_handle_t handle, WriteOptions write_options, const WriteArgs& args) {
  HandleDisposition disposition(args);
  return zx_channel_write_etc(handle, ZX_CHANNEL_WRITE_USE_IOVEC, args.data, args.data_count,
                              disposition.get(), args.handles_count);
}

zx_status_t channel_read(fidl_handle_t handle, const ReadOptions& read_options,
                         const ReadArgs& args) {
  ZX_DEBUG_ASSERT(args.storage_view != nullptr);
  ZX_DEBUG_ASSERT(args.out_data != nullptr);
  ChannelMessageStorageView* rd_view = static_cast<ChannelMessageStorageView*>(args.storage_view);

  uint32_t options = 0;
  if (read_options.discardable) {
    options |= ZX_CHANNEL_READ_MAY_DISCARD;
  }

  *args.out_data_actual_count = 0;
  *args.out_handles_actual_count = 0;
  HandleInfo info;
  zx_status_t status = zx_channel_read_etc(
      handle, options, rd_view->bytes.data, info.get(), rd_view->bytes.capacity,
      rd_view->handle_capacity, args.out_data_actual_count, args.out_handles_actual_count);
  if (status != ZX_OK) {
    return status;
  }

  *args.out_data = rd_view->bytes.data;
  *args.out_handles = rd_view->handles;
  *args.out_handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(rd_view->handle_metadata);
  info.Read(args);
  return ZX_OK;
}

zx_status_t channel_call(fidl_handle_t handle, CallOptions call_options,
                         const CallMethodArgs& args) {
  ZX_DEBUG_ASSERT(args.rd.storage_view != nullptr);
  ZX_DEBUG_ASSERT(args.rd.out_data != nullptr);
  ChannelMessageStorageView* rd_view =
      static_cast<ChannelMessageStorageView*>(args.rd.storage_view);

  HandleDisposition disposition(args.wr);
  HandleInfo info;
  zx_channel_call_etc_args_t zircon_args = {
      .wr_bytes = args.wr.data,
      .wr_handles = disposition.get(),
      .rd_bytes = rd_view->bytes.data,
      .rd_handles = info.get(),
      .wr_num_bytes = args.wr.data_count,
      .wr_num_handles = args.wr.handles_count,
      .rd_num_bytes = rd_view->bytes.capacity,
      .rd_num_handles = rd_view->handle_capacity,
  };

  zx_status_t status =
      zx_channel_call_etc(handle, ZX_CHANNEL_WRITE_USE_IOVEC, ZX_TIME_INFINITE, &zircon_args,
                          args.rd.out_data_actual_count, args.rd.out_handles_actual_count);
  if (status != ZX_OK) {
    return status;
  }

  *args.rd.out_data = rd_view->bytes.data;
  *args.rd.out_handles = rd_view->handles;
  *args.rd.out_handle_metadata =
      reinterpret_cast<fidl_handle_metadata_t*>(rd_view->handle_metadata);
  info.Read(args.rd);
  return ZX_OK;
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
void channel_close_many(const fidl_handle_t* handles, size_t num_handles) {
  zx_handle_close_many(handles, num_handles);
}

}  // namespace

const TransportVTable ChannelTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_CHANNEL,
    .encoding_configuration = &ChannelTransport::EncodingConfiguration,
    .write = channel_write,
    .read = channel_read,
    .call = channel_call,
    .create_waiter = channel_create_waiter,
};

zx_status_t ChannelWaiter::Begin() {
  zx_status_t status = async_begin_wait(dispatcher_, static_cast<async_wait_t*>(this));
  if (status == ZX_ERR_BAD_STATE) {
    // async_begin_wait return ZX_ERR_BAD_STATE if the dispatcher is shutting down.
    return ZX_ERR_CANCELED;
  }
  return status;
}

TransportWaiter::CancellationResult ChannelWaiter::Cancel() {
  zx_status_t status = async_cancel_wait(dispatcher_, static_cast<async_wait_t*>(this));
  switch (status) {
    case ZX_OK:
      return CancellationResult::kOk;
    case ZX_ERR_NOT_FOUND:
      return CancellationResult::kNotFound;
    case ZX_ERR_NOT_SUPPORTED:
      return CancellationResult::kNotSupported;
    default:
      ZX_PANIC("Unexpected status from async_cancel_wait: %d", status);
  }
}

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
  ChannelMessageStorageView storage_view{
      .bytes = bytes.view(),
      .handles = handles,
      .handle_metadata = handle_metadata,
      .handle_capacity = ZX_CHANNEL_MAX_MSG_HANDLES,
  };
  fidl_trace(WillLLCPPAsyncChannelRead);
  IncomingHeaderAndMessage msg =
      fidl::MessageRead(zx::unowned_channel(async_wait_t::object), storage_view);
  if (!msg.ok()) {
    return failure_handler_(fidl::UnbindInfo{msg});
  }
  fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes.data(), msg.byte_actual(),
             msg.handle_actual());
  return success_handler_(msg, &storage_view);
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
    .handle_metadata_stride = sizeof(fidl_channel_handle_metadata_t),
    .encode_process_handle = channel_encode_process_handle,
    .decode_process_handle = channel_decode_process_handle,
    .close = channel_close,
    .close_many = channel_close_many,
};

}  // namespace internal
}  // namespace fidl
