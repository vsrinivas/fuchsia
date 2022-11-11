// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/outgoing_message.h>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
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

uint32_t OutgoingMessage::CountBytes() const {
  uint32_t byte_count = 0;
  for (uint32_t i = 0; i < iovec_actual(); ++i) {
    byte_count += iovecs()[i].capacity;
  }
  return byte_count;
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
  if (unlikely(!ok())) {
    return;
  }
  if (unlikely(wire_format_version != fidl::internal::WireFormatVersion::kV2)) {
    SetStatus(fidl::Status::EncodeError(ZX_ERR_INVALID_ARGS, "only v2 wire format supported"));
    return;
  }

  fit::result<fidl::Error, fidl::internal::WireEncoder::Result> result = fidl::internal::WireEncode(
      inline_size, encode_fn, transport_vtable_->encoding_configuration, data, iovecs(),
      iovec_capacity(), handles(), message_.iovec.handle_metadata, handle_capacity(),
      backing_buffer(), backing_buffer_capacity());
  if (unlikely(!result.is_ok())) {
    SetStatus(result.error_value());
    return;
  }
  iovec_message().num_iovecs = static_cast<uint32_t>(result.value().iovec_actual);
  iovec_message().num_handles = static_cast<uint32_t>(result.value().handle_actual);
}

void OutgoingMessage::Write(internal::AnyUnownedTransport transport, WriteOptions options) {
  if (unlikely(!ok())) {
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
  if (unlikely(status != ZX_OK)) {
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

}  // namespace fidl
