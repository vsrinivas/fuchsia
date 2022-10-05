// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstring>
#include <string>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/server.h>
#include <zircon/syscalls.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif  // __Fuchsia__

namespace fidl {

OutgoingMessage OutgoingMessage::FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg, true);
}

OutgoingMessage OutgoingMessage::FromEncodedCValue(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg, false);
}

OutgoingMessage::OutgoingMessage(const fidl_outgoing_msg_t* c_msg, bool is_transactional)
    : fidl::Status(fidl::Status::Ok()) {
  ZX_ASSERT(c_msg);
  transport_vtable_ = &internal::ChannelTransport::VTable;
  switch (c_msg->type) {
    case FIDL_OUTGOING_MSG_TYPE_IOVEC: {
      message_ = *c_msg;
      iovec_capacity_ = c_msg->iovec.num_iovecs;
      handle_capacity_ = c_msg->iovec.num_handles;
      break;
    }
    case FIDL_OUTGOING_MSG_TYPE_BYTE: {
      backing_buffer_ = reinterpret_cast<uint8_t*>(c_msg->byte.bytes);
      backing_buffer_capacity_ = c_msg->byte.num_bytes;
      converted_byte_message_iovec_ = {
          .buffer = backing_buffer_,
          .capacity = backing_buffer_capacity_,
          .reserved = 0,
      };
      message_ = {
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec =
              {
                  .iovecs = &converted_byte_message_iovec_,
                  .num_iovecs = 1,
                  .handles = c_msg->byte.handles,
                  .handle_metadata = c_msg->byte.handle_metadata,
                  .num_handles = c_msg->byte.num_handles,
              },
      };
      iovec_capacity_ = 1;
      handle_capacity_ = c_msg->byte.num_handles;
      break;
    }
    default:
      ZX_PANIC("unhandled FIDL outgoing message type");
  }
  is_transactional_ = is_transactional;
}

OutgoingMessage::OutgoingMessage(const ::fidl::Status& failure)
    : fidl::Status(failure),
      message_({.type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
                .iovec = {
                    .iovecs = nullptr,
                    .num_iovecs = 0,
                    .handles = nullptr,
                    .handle_metadata = nullptr,
                    .num_handles = 0,
                }}) {
  ZX_DEBUG_ASSERT(failure.status() != ZX_OK);
}

OutgoingMessage::OutgoingMessage(InternalIovecConstructorArgs args)
    : fidl::Status(fidl::Status::Ok()),
      transport_vtable_(args.transport_vtable),
      message_({
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec = {.iovecs = args.iovecs,
                    .num_iovecs = 0,
                    .handles = args.handles,
                    .handle_metadata = args.handle_metadata,
                    .num_handles = 0},
      }),
      iovec_capacity_(args.iovec_capacity),
      handle_capacity_(args.handle_capacity),
      backing_buffer_capacity_(args.backing_buffer_capacity),
      backing_buffer_(args.backing_buffer),
      is_transactional_(args.is_transactional) {}

OutgoingMessage::OutgoingMessage(InternalByteBackedConstructorArgs args)
    : fidl::Status(fidl::Status::Ok()),
      transport_vtable_(args.transport_vtable),
      message_({
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec =
              {
                  .iovecs = &converted_byte_message_iovec_,
                  .num_iovecs = 1,
                  .handles = args.handles,
                  .handle_metadata = args.handle_metadata,
                  .num_handles = args.num_handles,
              },
      }),
      iovec_capacity_(1),
      handle_capacity_(args.num_handles),
      backing_buffer_capacity_(args.num_bytes),
      backing_buffer_(args.bytes),
      converted_byte_message_iovec_(
          {.buffer = backing_buffer_, .capacity = backing_buffer_capacity_, .reserved = 0}),
      is_transactional_(args.is_transactional) {}

OutgoingMessage::~OutgoingMessage() {
  // We may not have a vtable when the |OutgoingMessage| represents an error.
  if (transport_vtable_) {
    transport_vtable_->encoding_configuration->close_many(handles(), handle_actual());
  }
}

fidl_outgoing_msg_t OutgoingMessage::ReleaseToEncodedCMessage() && {
  ZX_DEBUG_ASSERT(status() == ZX_OK);
  ZX_ASSERT(transport_type() == FIDL_TRANSPORT_TYPE_CHANNEL);
  fidl_outgoing_msg_t result = message_;
  ReleaseHandles();
  return result;
}

bool OutgoingMessage::BytesMatch(const OutgoingMessage& other) const {
  uint32_t iovec_index = 0, other_iovec_index = 0;
  uint32_t byte_index = 0, other_byte_index = 0;
  while (iovec_index < iovec_actual() && other_iovec_index < other.iovec_actual()) {
    zx_channel_iovec_t cur_iovec = iovecs()[iovec_index];
    zx_channel_iovec_t other_cur_iovec = other.iovecs()[other_iovec_index];
    const uint8_t* cur_bytes = reinterpret_cast<const uint8_t*>(cur_iovec.buffer);
    const uint8_t* other_cur_bytes = reinterpret_cast<const uint8_t*>(other_cur_iovec.buffer);

    uint32_t cmp_len =
        std::min(cur_iovec.capacity - byte_index, other_cur_iovec.capacity - other_byte_index);
    if (memcmp(&cur_bytes[byte_index], &other_cur_bytes[other_byte_index], cmp_len) != 0) {
      return false;
    }

    byte_index += cmp_len;
    if (byte_index == cur_iovec.capacity) {
      iovec_index++;
      byte_index = 0;
    }
    other_byte_index += cmp_len;
    if (other_byte_index == other_cur_iovec.capacity) {
      other_iovec_index++;
      other_byte_index = 0;
    }
  }
  return iovec_index == iovec_actual() && other_iovec_index == other.iovec_actual() &&
         byte_index == 0 && other_byte_index == 0;
}

void OutgoingMessage::EncodeImpl(fidl::internal::WireFormatVersion wire_format_version, void* data,
                                 size_t inline_size, fidl::internal::TopLevelEncodeFn encode_fn) {
  if (!ok()) {
    return;
  }
  if (wire_format_version != fidl::internal::WireFormatVersion::kV2) {
    SetStatus(fidl::Status::EncodeError(ZX_ERR_INVALID_ARGS, "only v2 wire format supported"));
    return;
  }

  fit::result<fidl::Error, fidl::internal::WireEncoder::Result> result = fidl::internal::WireEncode(
      inline_size, encode_fn, transport_vtable_->encoding_configuration, data, iovecs(),
      iovec_capacity(), handles(), message_.iovec.handle_metadata, handle_capacity(),
      backing_buffer(), backing_buffer_capacity());
  if (!result.is_ok()) {
    SetStatus(result.error_value());
    return;
  }
  iovec_message().num_iovecs = static_cast<uint32_t>(result.value().iovec_actual);
  iovec_message().num_handles = static_cast<uint32_t>(result.value().handle_actual);

  if (is_transactional()) {
    ZX_ASSERT(iovec_actual() >= 1 && iovecs()[0].capacity >= sizeof(fidl_message_header_t));
    static_cast<fidl_message_header_t*>(const_cast<void*>(iovecs()[0].buffer))->at_rest_flags[0] |=
        FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2;
  }
}

void OutgoingMessage::Write(internal::AnyUnownedTransport transport, WriteOptions options) {
  if (!ok()) {
    return;
  }
  ZX_ASSERT(transport_type() == transport.type());
  ZX_ASSERT(is_transactional());
  zx_status_t status = transport.write(
      std::move(options), internal::WriteArgs{.data = iovecs(),
                                              .handles = handles(),
                                              .handle_metadata = message_.iovec.handle_metadata,
                                              .data_count = iovec_actual(),
                                              .handles_count = handle_actual()});
  ReleaseHandles();
  if (status != ZX_OK) {
    SetStatus(fidl::Status::TransportError(status));
  }
}

IncomingHeaderAndMessage OutgoingMessage::CallImpl(internal::AnyUnownedTransport transport,
                                                   internal::MessageStorageViewBase& storage,
                                                   CallOptions options) {
  if (status() != ZX_OK) {
    return IncomingHeaderAndMessage::Create(Status(*this));
  }
  ZX_ASSERT(transport_type() == transport.type());
  ZX_ASSERT(is_transactional());

  uint8_t* result_bytes;
  fidl_handle_t* result_handles;
  fidl_handle_metadata_t* result_handle_metadata;
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  internal::CallMethodArgs args = {
      .wr =
          internal::WriteArgs{
              .data = iovecs(),
              .handles = handles(),
              .handle_metadata = message_.iovec.handle_metadata,
              .data_count = iovec_actual(),
              .handles_count = handle_actual(),
          },
      .rd =
          internal::ReadArgs{
              .storage_view = &storage,
              .out_data = reinterpret_cast<void**>(&result_bytes),
              .out_handles = &result_handles,
              .out_handle_metadata = &result_handle_metadata,
              .out_data_actual_count = &actual_num_bytes,
              .out_handles_actual_count = &actual_num_handles,
          },
  };

  zx_status_t status = transport.call(std::move(options), args);
  ReleaseHandles();
  if (status != ZX_OK) {
    SetStatus(fidl::Status::TransportError(status));
    return IncomingHeaderAndMessage::Create(Status(*this));
  }

  return IncomingHeaderAndMessage(transport_vtable_, result_bytes, actual_num_bytes, result_handles,
                                  result_handle_metadata, actual_num_handles);
}

OutgoingMessage::CopiedBytes::CopiedBytes(const OutgoingMessage& msg) {
  uint32_t byte_count = 0;
  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    byte_count += msg.iovecs()[i].capacity;
  }
  bytes_.reserve(byte_count);
  for (uint32_t i = 0; i < msg.iovec_actual(); ++i) {
    zx_channel_iovec_t iovec = msg.iovecs()[i];
    const uint8_t* buf_bytes = reinterpret_cast<const uint8_t*>(iovec.buffer);
    bytes_.insert(bytes_.end(), buf_bytes, buf_bytes + iovec.capacity);
  }
}

OutgoingToIncomingMessage::OutgoingToIncomingMessage(OutgoingMessage& input)
    : incoming_message_(
          ConversionImpl(input, buf_bytes_, buf_handles_, buf_handle_metadata_, status_)) {}

[[nodiscard]] std::string OutgoingToIncomingMessage::FormatDescription() const {
  return status_.FormatDescription();
}

EncodedMessage OutgoingToIncomingMessage::ConversionImpl(
    OutgoingMessage& input, OutgoingMessage::CopiedBytes& buf_bytes,
    std::unique_ptr<zx_handle_t[]>& buf_handles,
    // TODO(fxbug.dev/85734) Remove channel-specific logic.
    std::unique_ptr<fidl_channel_handle_metadata_t[]>& buf_handle_metadata,
    fidl::Status& out_status) {
  ZX_ASSERT(!input.is_transactional());

  zx_handle_t* handles = input.handles();
  fidl_channel_handle_metadata_t* handle_metadata =
      input.handle_metadata<fidl::internal::ChannelTransport>();
  uint32_t num_handles = input.handle_actual();
  input.ReleaseHandles();

  // Note: it may be possible to remove these allocations.
  buf_handles = std::make_unique<zx_handle_t[]>(num_handles);
  buf_handle_metadata = std::make_unique<fidl_channel_handle_metadata_t[]>(num_handles);
  for (uint32_t i = 0; i < num_handles; i++) {
    const char* error;
    zx_status_t status = FidlEnsureActualHandleRights(&handles[i], handle_metadata[i].obj_type,
                                                      handle_metadata[i].rights, &error);
    if (status != ZX_OK) {
      FidlHandleCloseMany(handles, num_handles);
      FidlHandleCloseMany(buf_handles.get(), num_handles);
      out_status = fidl::Status::EncodeError(status);
      return fidl::EncodedMessage::Create({});
    }
    buf_handles[i] = handles[i];
    buf_handle_metadata[i] = handle_metadata[i];
  }

  buf_bytes = input.CopyBytes();
  out_status = fidl::Status::Ok();
  return fidl::EncodedMessage::Create(cpp20::span<uint8_t>{buf_bytes}, buf_handles.get(),
                                      buf_handle_metadata.get(), num_handles);
}

}  // namespace fidl
