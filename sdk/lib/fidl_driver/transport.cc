// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// The MakeAnyTransport overloads need to be defined before including
// message.h, which uses them.
#include <lib/fidl_driver/cpp/transport.h>
// clang-format on

#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/result.h>
#include <zircon/errors.h>

#include <optional>

namespace fidl {
namespace internal {

namespace {
zx_status_t driver_write(fidl_handle_t handle, WriteOptions write_options, const void* data,
                         uint32_t data_count, const fidl_handle_t* handles,
                         const void* handle_metadata, uint32_t handles_count) {
  // Note: in order to force the encoder to only output one iovec, only provide an iovec buffer of
  // 1 element to the encoder.
  ZX_ASSERT(data_count == 1);

  const zx_channel_iovec_t& iovec = static_cast<const zx_channel_iovec_t*>(data)[0];
  fdf_arena_t* arena =
      write_options.outgoing_transport_context.release<internal::DriverTransport>();
  void* arena_handles = fdf_arena_allocate(arena, handles_count * sizeof(fdf_handle_t));
  memcpy(arena_handles, handles, handles_count * sizeof(fdf_handle_t));

  zx_status_t status =
      fdf_channel_write(handle, 0, arena, const_cast<void*>(iovec.buffer), iovec.capacity,
                        static_cast<fdf_handle_t*>(arena_handles), handles_count);
  return status;
}

zx_status_t driver_read(fidl_handle_t handle, const ReadOptions& read_options, void* data,
                        uint32_t data_capacity, fidl_handle_t* handles, void* handle_metadata,
                        uint32_t handles_capacity, uint32_t* out_data_actual_count,
                        uint32_t* out_handles_actual_count) {
  fdf_arena_t* out_arena;
  void* out_data;
  uint32_t out_num_bytes;
  fidl_handle_t* out_handles;
  uint32_t out_num_handles;
  zx_status_t status = fdf_channel_read(handle, 0, &out_arena, &out_data, &out_num_bytes,
                                        &out_handles, &out_num_handles);
  if (status != ZX_OK) {
    return status;
  }
  if (out_num_bytes > data_capacity) {
    fdf_arena_destroy(out_arena);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (out_num_handles > handles_capacity) {
    fdf_arena_destroy(out_arena);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(data, out_data, out_num_bytes);
  memcpy(handles, out_handles, out_num_handles * sizeof(fidl_handle_t));
  *out_data_actual_count = out_num_bytes;
  *out_handles_actual_count = out_num_handles;

  *read_options.out_incoming_transport_context =
      internal::IncomingTransportContext::Create<internal::DriverTransport>(out_arena);

  return ZX_OK;
}

zx_status_t driver_call(fidl_handle_t handle, CallOptions call_options, const CallMethodArgs& cargs,
                        uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) {
  ZX_DEBUG_ASSERT(cargs.rd_data == nullptr);
  ZX_DEBUG_ASSERT(cargs.out_rd_data != nullptr);

  // Note: in order to force the encoder to only output one iovec, only provide an iovec buffer of
  // 1 element to the encoder.
  ZX_ASSERT(cargs.wr_data_count == 1);
  const zx_channel_iovec_t& iovec = static_cast<const zx_channel_iovec_t*>(cargs.wr_data)[0];
  fdf_arena_t* arena = call_options.outgoing_transport_context.release<DriverTransport>();

  void* arena_handles = fdf_arena_allocate(arena, cargs.wr_handles_count * sizeof(fdf_handle_t));
  memcpy(arena_handles, cargs.wr_handles, cargs.wr_handles_count * sizeof(fdf_handle_t));

  fdf_arena_t* rd_arena;
  fdf_handle_t* rd_handles;
  fdf_channel_call_args args = {
      .wr_arena = arena,
      .wr_data = const_cast<void*>(iovec.buffer),
      .wr_num_bytes = iovec.capacity,
      .wr_handles = static_cast<fdf_handle_t*>(arena_handles),
      .wr_num_handles = cargs.wr_handles_count,

      .rd_arena = &rd_arena,
      .rd_data = cargs.out_rd_data,
      .rd_num_bytes = out_data_actual_count,
      .rd_handles = cargs.out_rd_handles,
      .rd_num_handles = out_handles_actual_count,
  };
  zx_status_t status = fdf_channel_call(handle, 0, ZX_TIME_INFINITE, &args);
  if (status != ZX_OK) {
    return status;
  }
  memcpy(cargs.rd_handles, rd_handles, *out_handles_actual_count * sizeof(fdf_handle_t));
  *call_options.out_incoming_transport_context =
      IncomingTransportContext::Create<DriverTransport>(rd_arena);

  return ZX_OK;
}

zx_status_t driver_create_waiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                                 TransportWaitSuccessHandler success_handler,
                                 TransportWaitFailureHandler failure_handler,
                                 AnyTransportWaiter& any_transport_waiter) {
  any_transport_waiter.emplace<DriverWaiter>(handle, dispatcher, std::move(success_handler),
                                             std::move(failure_handler));
  return ZX_OK;
}

void driver_close(fidl_handle_t handle) { fdf_handle_close(handle); }

void driver_close_context(void* arena) { fdf_arena_destroy(static_cast<fdf_arena_t*>(arena)); }

}  // namespace

const TransportVTable DriverTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_DRIVER,
    .encoding_configuration = &DriverTransport::EncodingConfiguration,
    .write = driver_write,
    .read = driver_read,
    .call = driver_call,
    .create_waiter = driver_create_waiter,
    .close = driver_close,

    // The arena in the incoming context is owned, while the arena in the outgoing context is
    // borrowed (and does not require a custom close function).
    .close_incoming_transport_context = driver_close_context,
};

zx_status_t DriverWaiter::Begin() {
  state_.channel_read.emplace(
      state_.handle, 0 /* options */,
      [&state = state_](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                        fdf_status_t status) {
        if (status != ZX_OK) {
          fidl::UnbindInfo unbind_info;
          if (status == ZX_ERR_PEER_CLOSED) {
            unbind_info = fidl::UnbindInfo::PeerClosed(status);
          } else {
            unbind_info = fidl::UnbindInfo::DispatcherError(status);
          }
          return state.failure_handler(unbind_info);
        }

        fdf_arena_t* arena;
        void* data;
        uint32_t num_bytes;
        fidl_handle_t* handles;
        uint32_t num_handles;
        status =
            fdf_channel_read(state.handle, 0, &arena, &data, &num_bytes, &handles, &num_handles);
        if (status != ZX_OK) {
          return state.failure_handler(fidl::UnbindInfo(fidl::Result::TransportError(status)));
        }

        internal::IncomingTransportContext incoming_transport_context =
            internal::IncomingTransportContext::Create<fidl::internal::DriverTransport>(arena);
        fidl::IncomingMessage msg = fidl::IncomingMessage::Create<fidl::internal::DriverTransport>(
            static_cast<uint8_t*>(data), num_bytes, handles, nullptr, num_handles);
        state.channel_read = std::nullopt;
        return state.success_handler(msg, std::move(incoming_transport_context));
      });
  return state_.channel_read->Begin(fdf_dispatcher_from_async_dispatcher(state_.dispatcher));
}

zx_status_t DriverWaiter::Cancel() {
  fdf_dispatcher_t* dispatcher = fdf_dispatcher_from_async_dispatcher(state_.dispatcher);
  uint32_t options = fdf_dispatcher_get_options(dispatcher);
  fdf_status_t status = state_.channel_read->Cancel();
  if (status != ZX_OK) {
    return status;
  }
  // When the dispatcher is synchronized, our |ChannelRead| handler won't be
  // called. When the dispatcher is unsynchronized, our |ChannelRead| handler
  // will always be called (sometimes with a ZX_OK status and othertimes with a
  // ZX_ERR_CANCELED status). For the purpose of determining which code should
  // finish teardown of the |AsyncBinding|, it is as if the cancellation failed.
  if (options & FDF_DISPATCHER_OPTION_UNSYNCHRONIZED) {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

const CodingConfig DriverTransport::EncodingConfiguration = {
    .max_iovecs_write = 1,
};

}  // namespace internal
}  // namespace fidl
