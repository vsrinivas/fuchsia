// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/transformer.h>
#include <zircon/assert.h>

#include <cstring>
#include <string>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/syscalls.h>
#endif  // __Fuchsia__

namespace fidl {

OutgoingMessage OutgoingMessage::FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg);
}

OutgoingMessage::OutgoingMessage(const fidl_outgoing_msg_t* c_msg)
    : fidl::Result(fidl::Result::Ok()) {
  ZX_ASSERT(c_msg);
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
                  .transport_type = c_msg->byte.transport_type,
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
  is_transactional_ = true;
}

OutgoingMessage::OutgoingMessage(const ::fidl::Result& failure)
    : fidl::Result(failure),
      message_({.type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
                .iovec = {
                    .transport_type = FIDL_TRANSPORT_TYPE_INVALID,
                    .iovecs = nullptr,
                    .num_iovecs = 0,
                    .handles = nullptr,
                    .handle_metadata = nullptr,
                    .num_handles = 0,
                }}) {
  ZX_DEBUG_ASSERT(failure.status() != ZX_OK);
}

OutgoingMessage::OutgoingMessage(ConstructorArgs args)
    : fidl::Result(fidl::Result::Ok()),
      message_({
          .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
          .iovec = {.transport_type = args.transport_type,
                    .iovecs = args.iovecs,
                    .num_iovecs = 0,
                    .handles = args.handles,
                    .handle_metadata = args.handle_metadata,
                    .num_handles = 0},
      }),
      iovec_capacity_(args.iovec_capacity),
      handle_capacity_(args.handle_capacity),
      backing_buffer_capacity_(args.backing_buffer_capacity),
      backing_buffer_(args.backing_buffer) {}

OutgoingMessage::~OutgoingMessage() {
#ifdef __Fuchsia__
  if (handle_actual() > 0) {
    FidlHandleCloseMany(handles(), handle_actual());
  }
#else
  ZX_ASSERT(handle_actual() == 0);
#endif
}

fidl_outgoing_msg_t OutgoingMessage::ReleaseToEncodedCMessage() && {
  ZX_DEBUG_ASSERT(status() == ZX_OK);
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

void OutgoingMessage::EncodeImpl(const fidl_type_t* message_type, void* data) {
  if (!ok()) {
    return;
  }
  zx_handle_disposition_t handle_dispositions[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t num_iovecs_actual;
  uint32_t num_handles_actual;
  zx_status_t status;
  status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      message_type, data, iovecs(), iovec_capacity(), handle_dispositions, handle_capacity(),
      backing_buffer(), backing_buffer_capacity(), &num_iovecs_actual, &num_handles_actual,
      error_address());
  if (status != ZX_OK) {
    SetResult(fidl::Result::EncodeError(status, *error_address()));
    return;
  }
  iovec_message().num_iovecs = num_iovecs_actual;
  iovec_message().num_handles = num_handles_actual;
  fidl_channel_handle_metadata_t* metadata =
      static_cast<fidl_channel_handle_metadata_t*>(iovec_message().handle_metadata);
  for (uint32_t i = 0; i < num_handles_actual; i++) {
    iovec_message().handles[i] = handle_dispositions[i].handle;
    metadata[i] = {
        .obj_type = handle_dispositions[i].type,
        .rights = handle_dispositions[i].rights,
    };
  }

  auto linearized_bytes = CopyBytes();
  uint32_t actual_num_bytes;
  status = internal__fidl_transform__may_break(
      FIDL_TRANSFORMATION_V2_TO_V1, message_type, linearized_bytes.data(), linearized_bytes.size(),
      backing_buffer_, backing_buffer_capacity_, &actual_num_bytes, error_address());
  if (status != ZX_OK) {
    SetResult(fidl::Result::EncodeError(status, *error_address()));
    return;
  }

  converted_byte_message_iovec_ = {
      .buffer = backing_buffer_,
      .capacity = actual_num_bytes,
  };
  message_.type = FIDL_OUTGOING_MSG_TYPE_IOVEC;
  message_.iovec.iovecs = &converted_byte_message_iovec_;
  message_.iovec.num_iovecs = 1;
}

#ifdef __Fuchsia__
void OutgoingMessage::WriteImpl(zx_handle_t channel) {
  if (!ok()) {
    return;
  }
  zx_handle_disposition_t input_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t* metadata =
      static_cast<fidl_channel_handle_metadata_t*>(handle_metadata());
  for (uint32_t i = 0; i < handle_actual(); i++) {
    input_handles[i] = {
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = handles()[i],
        .type = metadata[i].obj_type,
        .rights = metadata[i].rights,
        .result = ZX_OK,
    };
  }
  zx_status_t status = zx_channel_write_etc(channel, ZX_CHANNEL_WRITE_USE_IOVEC, iovecs(),
                                            iovec_actual(), input_handles, handle_actual());
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::TransportError(status));
  }
}

void OutgoingMessage::CallImpl(const fidl_type_t* response_type, zx_handle_t channel,
                               uint8_t* result_bytes, uint32_t result_capacity,
                               zx_time_t deadline) {
  if (status() != ZX_OK) {
    return;
  }
  zx_handle_disposition_t input_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t* metadata =
      static_cast<fidl_channel_handle_metadata_t*>(handle_metadata());
  for (uint32_t i = 0; i < handle_actual(); i++) {
    input_handles[i] = {
        .operation = ZX_HANDLE_OP_MOVE,
        .handle = handles()[i],
        .type = metadata[i].obj_type,
        .rights = metadata[i].rights,
        .result = ZX_OK,
    };
  }

  zx_handle_info_t result_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  zx_channel_call_etc_args_t args = {
      .wr_bytes = iovecs(),
      .wr_handles = input_handles,
      .rd_bytes = result_bytes,
      .rd_handles = result_handles,
      .wr_num_bytes = iovec_actual(),
      .wr_num_handles = handle_actual(),
      .rd_num_bytes = result_capacity,
      .rd_num_handles = ZX_CHANNEL_MAX_MSG_HANDLES,
  };

  zx_status_t status;
  status = zx_channel_call_etc(channel, ZX_CHANNEL_WRITE_USE_IOVEC, deadline, &args,
                               &actual_num_bytes, &actual_num_handles);
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::TransportError(status));
    return;
  }

  fidl_message_header_t header;
  memcpy(&header, result_bytes, sizeof(header));

  if ((header.flags[0] & FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2) == 0) {
    auto transformer_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);

    status = internal__fidl_transform__may_break(
        FIDL_TRANSFORMATION_V1_TO_V2, response_type, result_bytes, actual_num_bytes,
        transformer_bytes.get(), ZX_CHANNEL_MAX_MSG_BYTES, &actual_num_bytes, error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }

    if (actual_num_bytes > result_capacity) {
      SetResult(fidl::Result::DecodeError(ZX_ERR_BUFFER_TOO_SMALL,
                                          "transformed bytes exceeds message buffer capacity"));
      return;
    }
    memcpy(result_bytes, transformer_bytes.get(), actual_num_bytes);
  }

  status =
      internal_fidl_decode_etc__v2__may_break(response_type, result_bytes, actual_num_bytes,
                                              result_handles, actual_num_handles, error_address());
  if (status != ZX_OK) {
    SetResult(fidl::Result::DecodeError(status, *error_address()));
    return;
  }
}

#endif  // __Fuchsia__

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

IncomingMessage::IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                 fidl_transport_type transport_type, void* handle_metadata,
                                 uint32_t handle_actual)
    : IncomingMessage(bytes, byte_actual, handles, transport_type, handle_metadata, handle_actual,
                      kSkipMessageHeaderValidation) {
  Validate();
  is_transactional_ = true;
}

IncomingMessage IncomingMessage::FromEncodedCMessage(const fidl_incoming_msg_t* c_msg) {
  return IncomingMessage(reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes, c_msg->handles,
                         c_msg->transport_type, c_msg->handle_metadata, c_msg->num_handles);
}

IncomingMessage::IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                 fidl_transport_type transport_type, void* handle_metadata,
                                 uint32_t handle_actual, SkipMessageHeaderValidationTag)
    : fidl::Result(fidl::Result::Ok()),
      message_{
          .bytes = bytes,
          .handles = handles,
          .transport_type = transport_type,
          .handle_metadata = handle_metadata,
          .num_bytes = byte_actual,
          .num_handles = handle_actual,
      } {}

IncomingMessage::IncomingMessage(const fidl::Result& failure) : fidl::Result(failure), message_{} {
  ZX_DEBUG_ASSERT(failure.status() != ZX_OK);
}

IncomingMessage::~IncomingMessage() { std::move(*this).CloseHandles(); }

fidl_incoming_msg_t IncomingMessage::ReleaseToEncodedCMessage() && {
  ZX_DEBUG_ASSERT(status() == ZX_OK);
  fidl_incoming_msg_t result = message_;
  ReleaseHandles();
  return result;
}

void IncomingMessage::CloseHandles() && {
#ifdef __Fuchsia__
  if (handle_actual() > 0) {
    FidlHandleCloseMany(handles(), handle_actual());
  }
#else
  ZX_ASSERT(handle_actual() == 0);
#endif
  ReleaseHandles();
}

void IncomingMessage::Decode(const fidl_type_t* message_type,
                             std::unique_ptr<uint8_t[]>* out_transformed_buffer) {
  ZX_ASSERT(is_transactional_);
  internal::WireFormatVersion wire_format_version = internal::WireFormatVersion::kV1;
  if (bytes() != nullptr &&
      (header()->flags[0] & FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2) != 0) {
    wire_format_version = internal::WireFormatVersion::kV2;
  }
  Decode(wire_format_version, message_type, out_transformed_buffer);
}

void IncomingMessage::Decode(internal::WireFormatVersion wire_format_version,
                             const fidl_type_t* message_type,
                             std::unique_ptr<uint8_t[]>* out_transformed_buffer) {
  ZX_DEBUG_ASSERT(status() == ZX_OK);

  if (wire_format_version == internal::WireFormatVersion::kV1) {
    zx_status_t status = internal__fidl_validate__v1__may_break(
        message_type, bytes(), byte_actual(), handle_actual(), error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }

    *out_transformed_buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);

    uint32_t actual_num_bytes = 0;
    status = internal__fidl_transform__may_break(
        FIDL_TRANSFORMATION_V1_TO_V2, message_type, bytes(), byte_actual(),
        out_transformed_buffer->get(), ZX_CHANNEL_MAX_MSG_BYTES, &actual_num_bytes,
        error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }

    message_.bytes = out_transformed_buffer->get();
    message_.num_bytes = actual_num_bytes;
  }

  fidl_trace(WillLLCPPDecode, message_type, bytes(), byte_actual(), handle_actual());
  // TODO(fxbug.dev/85734) This assumes channel transport - remove the assumption.
  zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (uint32_t i = 0; i < message_.num_handles; i++) {
    handle_infos[i] = {
        .handle = message_.handles[i],
    };
    if (message_.handle_metadata) {
      const fidl_channel_handle_metadata_t* metadata =
          reinterpret_cast<const fidl_channel_handle_metadata_t*>(message_.handle_metadata);
      handle_infos[i].type = metadata[i].obj_type;
      handle_infos[i].rights = metadata[i].rights;
    }
  }
  zx_status_t status =
      internal_fidl_decode_etc__v2__may_break(message_type, message_.bytes, message_.num_bytes,
                                              handle_infos, message_.num_handles, error_address());
  fidl_trace(DidLLCPPDecode);
  // Now the caller is responsible for the handles contained in `bytes()`.
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::DecodeError(status, *error_address()));
  }
}

void IncomingMessage::Validate() {
  if (byte_actual() < sizeof(fidl_message_header_t)) {
    return SetResult(fidl::Result::UnexpectedMessage(ZX_ERR_INVALID_ARGS,
                                                     ::fidl::internal::kErrorInvalidHeader));
  }

  auto* hdr = header();
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    return SetResult(
        fidl::Result::UnexpectedMessage(status, ::fidl::internal::kErrorInvalidHeader));
  }

  // See https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0053_epitaphs?hl=en#wire_format
  if (unlikely(maybe_epitaph())) {
    if (hdr->txid != 0) {
      return SetResult(fidl::Result::UnexpectedMessage(ZX_ERR_INVALID_ARGS,
                                                       ::fidl::internal::kErrorInvalidHeader));
    }
  }
}

#ifdef __Fuchsia__

namespace internal {
IncomingMessage MessageRead(internal::AnyUnownedTransport transport, uint32_t options,
                            fidl::BufferSpan bytes_storage, zx_handle_t* handle_storage,
                            fidl_transport_type transport_type, void* handle_metadata_storage,
                            uint32_t handle_capacity) {
  // TODO(fxbug.dev/85734) Support abitrary transports.
  ZX_ASSERT(transport_type == FIDL_TRANSPORT_TYPE_CHANNEL);
  zx_handle_t channel = transport.get<internal::ChannelTransport>()->get();
  zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t num_bytes, num_handles;
  zx_status_t status =
      zx_channel_read_etc(channel, options, bytes_storage.data, handle_infos,
                          bytes_storage.capacity, handle_capacity, &num_bytes, &num_handles);
  if (status != ZX_OK) {
    return IncomingMessage(fidl::Result::TransportError(status));
  }
  fidl_channel_handle_metadata_t* metadata =
      reinterpret_cast<fidl_channel_handle_metadata_t*>(handle_metadata_storage);
  for (uint32_t i = 0; i < num_handles; i++) {
    handle_storage[i] = handle_infos[i].handle;
    metadata[i] = {
        .obj_type = handle_infos[i].type,
        .rights = handle_infos[i].rights,
    };
  }
  return IncomingMessage(bytes_storage.data, num_bytes, handle_storage, transport_type,
                         handle_metadata_storage, num_handles);
}
}  // namespace internal

#endif  // __Fuchsia__

OutgoingToIncomingMessage::OutgoingToIncomingMessage(OutgoingMessage& input)
    : incoming_message_(ConversionImpl(input, buf_bytes_, buf_handles_, buf_handle_metadata_)) {}

[[nodiscard]] std::string OutgoingToIncomingMessage::FormatDescription() const {
  return incoming_message_.FormatDescription();
}

IncomingMessage OutgoingToIncomingMessage::ConversionImpl(
    OutgoingMessage& input, OutgoingMessage::CopiedBytes& buf_bytes,
    std::unique_ptr<zx_handle_t[]>& buf_handles,
    // TODO(fxbug.dev/85734) Remove channel-specific logic.
    std::unique_ptr<fidl_channel_handle_metadata_t[]>& buf_handle_metadata) {
  constexpr fidl_transport_type conversion_transport_type = FIDL_TRANSPORT_TYPE_CHANNEL;
  zx_handle_t* handles = input.handles();
  fidl_channel_handle_metadata_t* handle_metadata =
      static_cast<fidl_channel_handle_metadata_t*>(input.handle_metadata());
  uint32_t num_handles = input.handle_actual();
  input.ReleaseHandles();

  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FidlHandleCloseMany(handles, num_handles);
    return fidl::IncomingMessage(fidl::Result::EncodeError(ZX_ERR_OUT_OF_RANGE));
  }

  // Note: it may be possible to remove these allocations.
  buf_handles = std::make_unique<zx_handle_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
  buf_handle_metadata =
      std::make_unique<fidl_channel_handle_metadata_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
  for (uint32_t i = 0; i < num_handles; i++) {
    const char* error;
    zx_status_t status = FidlEnsureActualHandleRights(&handles[i], handle_metadata[i].obj_type,
                                                      handle_metadata[i].rights, &error);
    if (status != ZX_OK) {
      FidlHandleCloseMany(handles, num_handles);
      FidlHandleCloseMany(buf_handles.get(), num_handles);
      return fidl::IncomingMessage(fidl::Result::EncodeError(status));
    }
    buf_handles[i] = handles[i];
    buf_handle_metadata[i] = handle_metadata[i];
  }

  buf_bytes = input.CopyBytes();
  if (buf_bytes.size() > ZX_CHANNEL_MAX_MSG_BYTES) {
    FidlHandleCloseMany(handles, num_handles);
    FidlHandleCloseMany(buf_handles.get(), num_handles);
    return fidl::IncomingMessage(fidl::Result::EncodeError(ZX_ERR_INVALID_ARGS));
  }

  if (input.is_transactional()) {
    return fidl::IncomingMessage(buf_bytes.data(), buf_bytes.size(), buf_handles.get(),
                                 conversion_transport_type, buf_handle_metadata.get(), num_handles);
  }
  return fidl::IncomingMessage(buf_bytes.data(), buf_bytes.size(), buf_handles.get(),
                               conversion_transport_type, buf_handle_metadata.get(), num_handles,
                               fidl::IncomingMessage::kSkipMessageHeaderValidation);
}

}  // namespace fidl
