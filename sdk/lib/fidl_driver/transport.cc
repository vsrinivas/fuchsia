// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// The MakeAnyTransport overloads need to be defined before including
// message.h, which uses them.
#include <lib/fidl_driver/cpp/transport.h>
// clang-format on

#include <lib/fdf/cpp/channel_read.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/message_storage.h>

#include <optional>

namespace fidl {
namespace internal {

namespace {
zx_status_t driver_write(fidl_handle_t handle, const WriteOptions& write_options, const void* data,
                         uint32_t data_count, const fidl_handle_t* handles,
                         const void* handle_metadata, uint32_t handles_count) {
  // Note: in order to force the encoder to only output one iovec, only provide an iovec buffer of
  // 1 element to the encoder.
  ZX_ASSERT(data_count == 1);
  ZX_ASSERT(handles_count == 0);
  ZX_ASSERT(handle_metadata == nullptr);

  const zx_channel_iovec_t& iovec = static_cast<const zx_channel_iovec_t*>(data)[0];
  fdf_arena_t* arena = reinterpret_cast<fdf_arena_t*>(write_options.outgoing_transport_context);
  void* arena_data = fdf_arena_allocate(arena, iovec.capacity);
  memcpy(arena_data, const_cast<void*>(iovec.buffer), iovec.capacity);

  // TODO(fxbug.dev/90646) Remove const_cast.
  zx_status_t status = fdf_channel_write(handle, 0, arena, arena_data, iovec.capacity,
                                         const_cast<fidl_handle_t*>(handles), handles_count);

  fdf_arena_destroy(arena);
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
      reinterpret_cast<internal::IncomingTransportContext*>(out_arena);

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

}  // namespace

const TransportVTable DriverTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_DRIVER,
    .encoding_configuration = &DriverTransport::EncodingConfiguration,
    .write = driver_write,
    .read = driver_read,
    .create_waiter = driver_create_waiter,
    .close = driver_close,
};

zx_status_t DriverWaiter::Begin() {
  state_->channel_read.emplace(
      state_->handle, 0 /* options */,
      // TODO(bprosnitz) Pass in a raw pointer after DriverWaiter::Cancel is implemented.
      [state = state_](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                       fdf_status_t status) {
        if (status != ZX_OK) {
          return state->failure_handler(fidl::UnbindInfo::DispatcherError(status));
        }

        FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT InlineMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES> bytes;
        FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT fidl_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
        internal::IncomingTransportContext* incoming_transport_context;
        fidl::ReadOptions read_options = {
            .out_incoming_transport_context = &incoming_transport_context,
        };
        IncomingMessage msg =
            fidl::MessageRead(fdf::UnownedChannel(state->handle), bytes.view(), handles, nullptr,
                              ZX_CHANNEL_MAX_MSG_HANDLES, read_options);
        if (!msg.ok()) {
          return state->failure_handler(fidl::UnbindInfo{msg});
        }
        state->channel_read = std::nullopt;
        return state->success_handler(msg, incoming_transport_context);
      });
  return state_->channel_read->Begin(fdf_dispatcher_from_async_dispatcher(state_->dispatcher));
}

const CodingConfig DriverTransport::EncodingConfiguration = {};

AnyTransport MakeAnyTransport(fdf::Channel channel) {
  return AnyTransport::Make<DriverTransport>(channel.release());
}
AnyUnownedTransport MakeAnyUnownedTransport(const fdf::Channel& channel) {
  return AnyUnownedTransport::Make<DriverTransport>(channel.get());
}
AnyUnownedTransport MakeAnyUnownedTransport(const fdf::UnownedChannel& channel) {
  return AnyUnownedTransport::Make<DriverTransport>(channel->get());
}

}  // namespace internal
}  // namespace fidl
