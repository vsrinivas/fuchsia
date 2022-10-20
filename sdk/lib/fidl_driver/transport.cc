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
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/fidl/cpp/wire/status.h>
#include <zircon/errors.h>

#include <optional>

namespace fidl {
namespace internal {

namespace {

zx_status_t driver_write(fidl_handle_t handle, WriteOptions write_options, const WriteArgs& args) {
  // Note: in order to force the encoder to only output one iovec, only provide an iovec buffer of
  // 1 element to the encoder.
  ZX_ASSERT(args.data_count == 1);

  const zx_channel_iovec_t& iovec = static_cast<const zx_channel_iovec_t*>(args.data)[0];
  fdf_arena_t* arena =
      write_options.outgoing_transport_context.release<internal::DriverTransport>();
  void* arena_handles = fdf_arena_allocate(arena, args.handles_count * sizeof(fdf_handle_t));
  memcpy(arena_handles, args.handles, args.handles_count * sizeof(fdf_handle_t));

  zx_status_t status =
      fdf_channel_write(handle, 0, arena, const_cast<void*>(iovec.buffer), iovec.capacity,
                        static_cast<fdf_handle_t*>(arena_handles), args.handles_count);
  return status;
}

zx_status_t driver_read(fidl_handle_t handle, const ReadOptions& read_options,
                        const ReadArgs& args) {
  ZX_DEBUG_ASSERT(args.storage_view != nullptr);
  ZX_DEBUG_ASSERT(args.out_data != nullptr);
  DriverMessageStorageView* rd_view = static_cast<DriverMessageStorageView*>(args.storage_view);

  fdf_arena_t* out_arena;
  zx_status_t status =
      fdf_channel_read(handle, 0, &out_arena, args.out_data, args.out_data_actual_count,
                       args.out_handles, args.out_handles_actual_count);
  if (status != ZX_OK) {
    return status;
  }

  *rd_view->arena = fdf::Arena(out_arena);
  return ZX_OK;
}

zx_status_t driver_call(fidl_handle_t handle, CallOptions call_options,
                        const CallMethodArgs& args) {
  ZX_DEBUG_ASSERT(args.rd.storage_view != nullptr);
  ZX_DEBUG_ASSERT(args.rd.out_data != nullptr);
  DriverMessageStorageView* rd_view = static_cast<DriverMessageStorageView*>(args.rd.storage_view);

  // Note: in order to force the encoder to only output one iovec, only provide an iovec buffer of
  // 1 element to the encoder.
  ZX_ASSERT(args.wr.data_count == 1);
  const zx_channel_iovec_t& iovec = static_cast<const zx_channel_iovec_t*>(args.wr.data)[0];
  fdf_arena_t* arena = call_options.outgoing_transport_context.release<DriverTransport>();
  void* arena_handles = fdf_arena_allocate(arena, args.wr.handles_count * sizeof(fdf_handle_t));
  memcpy(arena_handles, args.wr.handles, args.wr.handles_count * sizeof(fdf_handle_t));

  fdf_arena_t* rd_arena = nullptr;
  fdf_channel_call_args fdf_args = {
      .wr_arena = arena,
      .wr_data = const_cast<void*>(iovec.buffer),
      .wr_num_bytes = iovec.capacity,
      .wr_handles = static_cast<fdf_handle_t*>(arena_handles),
      .wr_num_handles = args.wr.handles_count,

      .rd_arena = &rd_arena,
      .rd_data = args.rd.out_data,
      .rd_num_bytes = args.rd.out_data_actual_count,
      .rd_handles = args.rd.out_handles,
      .rd_num_handles = args.rd.out_handles_actual_count,
  };
  zx_status_t status = fdf_channel_call(handle, 0, ZX_TIME_INFINITE, &fdf_args);
  if (status != ZX_OK) {
    return status;
  }

  *rd_view->arena = fdf::Arena(rd_arena);
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

void driver_close_many(const fidl_handle_t* handles, size_t num_handles) {
  for (size_t i = 0; i < num_handles; i++) {
    fdf_handle_close(handles[i]);
  }
}

}  // namespace

const TransportVTable DriverTransport::VTable = {
    .type = FIDL_TRANSPORT_TYPE_DRIVER,
    .encoding_configuration = &DriverTransport::EncodingConfiguration,
    .write = driver_write,
    .read = driver_read,
    .call = driver_call,
    .create_waiter = driver_create_waiter,
};

DriverWaiter::DriverWaiter(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                           TransportWaitSuccessHandler success_handler,
                           TransportWaitFailureHandler failure_handler)
    : handle_(handle),
      dispatcher_(dispatcher),
      success_handler_(std::move(success_handler)),
      failure_handler_(std::move(failure_handler)),
      channel_read_{fdf::ChannelRead{handle, 0 /* options */,
                                     fit::bind_member<&DriverWaiter::HandleChannelRead>(this)}} {}

void DriverWaiter::HandleChannelRead(fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                                     zx_status_t status) {
  if (status != ZX_OK) {
    fidl::UnbindInfo unbind_info;
    if (status == ZX_ERR_PEER_CLOSED) {
      unbind_info = fidl::UnbindInfo::PeerClosed(status);
    } else {
      unbind_info = fidl::UnbindInfo::DispatcherError(status);
    }
    return failure_handler_(unbind_info);
  }

  fdf::Arena arena(nullptr);
  DriverMessageStorageView storage_view{.arena = &arena};
  IncomingHeaderAndMessage msg = fidl::MessageRead(fdf::UnownedChannel(handle_), storage_view);
  if (!msg.ok()) {
    return failure_handler_(fidl::UnbindInfo{msg});
  }
  return success_handler_(msg, &storage_view);
}

zx_status_t DriverWaiter::Begin() {
  zx_status_t status = channel_read_.Begin(fdf_dispatcher_from_async_dispatcher(dispatcher_));
  if (status == ZX_ERR_UNAVAILABLE) {
    // Begin() is called when the dispatcher is shutting down.
    return ZX_ERR_CANCELED;
  }
  return status;
}

fidl::internal::DriverWaiter::CancellationResult DriverWaiter::Cancel() {
  fdf_dispatcher_t* dispatcher = fdf_dispatcher_from_async_dispatcher(dispatcher_);
  uint32_t options = fdf_dispatcher_get_options(dispatcher);

  if (options & FDF_DISPATCHER_OPTION_UNSYNCHRONIZED) {
    // Unsynchronized dispatcher.
    zx_status_t status = channel_read_.Cancel();
    ZX_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);

    // When the dispatcher is unsynchronized, our |ChannelRead| handler will
    // always be called (sometimes with a ZX_OK status and other times with a
    // ZX_ERR_CANCELED status). For the purpose of determining which code should
    // finish teardown of the |AsyncBinding|, it is as if the cancellation
    // failed.
    return CancellationResult::kNotFound;
  }

  // Synchronized dispatcher.
  fdf_dispatcher_t* current_dispatcher = fdf_dispatcher_get_current_dispatcher();
  if (current_dispatcher == dispatcher) {
    // The binding is being torn down from a dispatcher thread.
    zx_status_t status = channel_read_.Cancel();
    switch (status) {
      case ZX_OK:
        return CancellationResult::kOk;
      case ZX_ERR_NOT_FOUND:
        return CancellationResult::kNotFound;
      default:
        ZX_PANIC("Unsupported status: %d", status);
    }
  }

  // The binding is being torn down from a foreign thread.
  // The only way this could happen is when the user is using a shared client
  // or a server binding. In both cases, the contract is that the teardown
  // will happen asynchronously. We can implement that behavior by indicating
  // that synchronous cancellation failed.
  return CancellationResult::kDispatcherContextNeeded;
}

const CodingConfig DriverTransport::EncodingConfiguration = {
    .max_iovecs_write = 1,
    .handle_metadata_stride = 0,
    .close = driver_close,
    .close_many = driver_close_many,
};

}  // namespace internal
}  // namespace fidl
