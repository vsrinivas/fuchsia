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
#include <zircon/errors.h>

#include <cstring>
#include <string>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/server.h>
#include <zircon/syscalls.h>
#else
#include <lib/fidl/llcpp/internal/transport_channel_host.h>
#endif  // __Fuchsia__

namespace fidl {

OutgoingMessage OutgoingMessage::FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg, true);
}

OutgoingMessage OutgoingMessage::FromEncodedCValue(const fidl_outgoing_msg_t* c_msg) {
  return OutgoingMessage(c_msg, false);
}

OutgoingMessage::OutgoingMessage(const fidl_outgoing_msg_t* c_msg, bool is_transactional)
    : fidl::Result(fidl::Result::Ok()) {
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

OutgoingMessage::OutgoingMessage(const ::fidl::Result& failure)
    : fidl::Result(failure),
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

OutgoingMessage::OutgoingMessage(ConstructorArgs args)
    : fidl::Result(fidl::Result::Ok()),
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

void OutgoingMessage::EncodeImpl(fidl::internal::WireFormatVersion wire_format_version,
                                 const fidl_type_t* message_type, void* data) {
  if (!ok()) {
    return;
  }
  uint32_t num_iovecs_actual;
  uint32_t num_handles_actual;

  zx_status_t status = fidl::internal::EncodeIovecEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      *transport_vtable_->encoding_configuration, message_type, is_transactional(), data, iovecs(),
      iovec_capacity(), handles(), message_.iovec.handle_metadata, handle_capacity(),
      backing_buffer(), backing_buffer_capacity(), &num_iovecs_actual, &num_handles_actual,
      error_address());
  if (status != ZX_OK) {
    SetResult(fidl::Result::EncodeError(status, *error_address()));
    return;
  }
  iovec_message().num_iovecs = num_iovecs_actual;
  iovec_message().num_handles = num_handles_actual;

  if (wire_format_version == fidl::internal::WireFormatVersion::kV2) {
    if (is_transactional()) {
      ZX_ASSERT(iovec_actual() >= 1 && iovecs()[0].capacity >= sizeof(fidl_message_header_t));
      static_cast<fidl_message_header_t*>(const_cast<void*>(iovecs()[0].buffer))->flags[0] |=
          FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2;
    }
    return;
  }

  if (message_type == nullptr ||
      internal__fidl_tranform_is_noop__may_break(FIDL_TRANSFORMATION_V2_TO_V1, message_type)) {
    return;
  }

  auto linearized_bytes = CopyBytes();
  uint32_t actual_num_bytes;
  status = internal__fidl_transform__may_break(
      FIDL_TRANSFORMATION_V2_TO_V1, message_type, is_transactional(), linearized_bytes.data(),
      linearized_bytes.size(), backing_buffer(), backing_buffer_capacity(), &actual_num_bytes,
      error_address());
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

void OutgoingMessage::Validate__InternalMayBreak(
    fidl::internal::WireFormatVersion wire_format_version, const fidl_type_t* type) {
  zx_status_t status;
  auto copied_bytes = CopyBytes();
  const char* error_msg_out;
  switch (wire_format_version) {
    case internal::WireFormatVersion::kV1:
      status = internal__fidl_validate__v1__may_break(type, copied_bytes.data(),
                                                      static_cast<uint32_t>(copied_bytes.size()),
                                                      handle_actual(), &error_msg_out);
      break;
    case internal::WireFormatVersion::kV2:
      status = internal__fidl_validate__v2__may_break(type, copied_bytes.data(),
                                                      static_cast<uint32_t>(copied_bytes.size()),
                                                      handle_actual(), &error_msg_out);
      break;
    default:
      __builtin_unreachable();
  }
  if (status != ZX_OK)
    SetResult(Result::EncodeError(status, error_msg_out));
}

void OutgoingMessage::Write(internal::AnyUnownedTransport transport, WriteOptions options) {
  if (!ok()) {
    return;
  }
  ZX_ASSERT(transport_type() == transport.type());
  zx_status_t status = transport.write(std::move(options), iovecs(), iovec_actual(), handles(),
                                       message_.iovec.handle_metadata, handle_actual());
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::TransportError(status));
  }
}

void OutgoingMessage::DecodeImplForCall(const internal::CodingConfig& coding_config,
                                        const fidl_type_t* response_type, uint8_t* bytes,
                                        uint32_t* in_out_num_bytes, fidl_handle_t* handles,
                                        fidl_handle_metadata_t* handle_metadata,
                                        uint32_t num_handles) {
  fidl_message_header_t& header = reinterpret_cast<fidl_message_header_t&>(*bytes);
  if (response_type == nullptr) {
    return;
  } else if (unlikely(*in_out_num_bytes <= sizeof(fidl_message_header_t))) {
    SetResult(fidl::Result::DecodeError(ZX_ERR_BUFFER_TOO_SMALL,
                                        "non-nullptr response_type must be larger than 16 bytes"));
    return;
  }

  if ((header.flags[0] & FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2) == 0 &&
      response_type != nullptr &&
      !internal__fidl_tranform_is_noop__may_break(FIDL_TRANSFORMATION_V1_TO_V2, response_type)) {
    auto transformed_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
    uint32_t transformed_num_bytes = *in_out_num_bytes;

    zx_status_t status = internal__fidl_transform__may_break(
        FIDL_TRANSFORMATION_V1_TO_V2, response_type, true, bytes, *in_out_num_bytes,
        transformed_bytes.get(), ZX_CHANNEL_MAX_MSG_BYTES, &transformed_num_bytes, error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }

    if (transformed_num_bytes > *in_out_num_bytes) {
      SetResult(fidl::Result::DecodeError(ZX_ERR_BUFFER_TOO_SMALL,
                                          "transformed bytes exceeds message buffer capacity"));
      return;
    }

    memcpy(bytes, transformed_bytes.get(), transformed_num_bytes);
    *in_out_num_bytes = transformed_num_bytes;
  }

  uint8_t* trimmed_result_bytes;
  uint32_t trimmed_num_bytes;
  zx_status_t trim_status = ::fidl::internal::fidl_exclude_header_bytes(
      bytes, *in_out_num_bytes, &trimmed_result_bytes, &trimmed_num_bytes, error_address());
  if (trim_status != ZX_OK) {
    SetResult(fidl::Result::DecodeError(trim_status, *error_address()));
    return;
  }

  zx_status_t status = internal::DecodeEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      coding_config, response_type, trimmed_result_bytes, trimmed_num_bytes, handles,
      handle_metadata, num_handles, error_address());
  if (status != ZX_OK) {
    SetResult(fidl::Result::DecodeError(status, *error_address()));
    return;
  }
}

void OutgoingMessage::CallImplForTransportProvidedBuffer(internal::AnyUnownedTransport transport,
                                                         const fidl_type_t* response_type,
                                                         uint8_t** out_bytes,
                                                         uint32_t* out_num_bytes,
                                                         CallOptions options) {
  if (status() != ZX_OK) {
    return;
  }
  ZX_ASSERT(transport_type() == transport.type());

  fidl_handle_t* result_handles;
  fidl_handle_metadata_t* result_handle_metadata;
  uint32_t result_num_handles;
  internal::CallMethodArgs args = {
      .wr_data = iovecs(),
      .wr_handles = handles(),
      .wr_handle_metadata = message_.iovec.handle_metadata,
      .wr_data_count = iovec_actual(),
      .wr_handles_count = handle_actual(),
      .out_rd_data = reinterpret_cast<void**>(out_bytes),
      .out_rd_handles = &result_handles,
      .out_rd_handle_metadata = &result_handle_metadata,
  };

  zx_status_t status = transport.call(std::move(options), args, out_num_bytes, &result_num_handles);
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::TransportError(status));
    return;
  }

  DecodeImplForCall(*transport.vtable()->encoding_configuration, response_type, *out_bytes,
                    out_num_bytes, result_handles, result_handle_metadata, result_num_handles);
}

void OutgoingMessage::CallImplForCallerProvidedBuffer(
    internal::AnyUnownedTransport transport, const fidl_type_t* response_type,
    uint8_t* result_bytes, uint32_t result_byte_capacity, fidl_handle_t* result_handles,
    fidl_handle_metadata_t* result_handle_metadata, uint32_t result_handle_capacity,
    CallOptions options) {
  if (status() != ZX_OK) {
    return;
  }
  ZX_ASSERT(transport_type() == transport.type());

  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  internal::CallMethodArgs args = {
      .wr_data = iovecs(),
      .wr_handles = handles(),
      .wr_handle_metadata = message_.iovec.handle_metadata,
      .wr_data_count = iovec_actual(),
      .wr_handles_count = handle_actual(),
      .rd_data = result_bytes,
      .rd_handles = result_handles,
      .rd_handle_metadata = result_handle_metadata,
      .rd_data_capacity = result_byte_capacity,
      .rd_handles_capacity = result_handle_capacity,
  };

  zx_status_t status =
      transport.call(std::move(options), args, &actual_num_bytes, &actual_num_handles);
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::TransportError(status));
    return;
  }

  DecodeImplForCall(*transport.vtable()->encoding_configuration, response_type, result_bytes,
                    &actual_num_bytes, result_handles, result_handle_metadata, actual_num_handles);
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

IncomingMessage::IncomingMessage(const internal::TransportVTable* transport_vtable, uint8_t* bytes,
                                 uint32_t byte_actual, zx_handle_t* handles,
                                 fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual)
    : IncomingMessage(transport_vtable, bytes, byte_actual, handles, handle_metadata, handle_actual,
                      kSkipMessageHeaderValidation) {
  ValidateHeader();
  is_transactional_ = true;
}

IncomingMessage IncomingMessage::FromEncodedCMessage(const fidl_incoming_msg_t* c_msg) {
  return IncomingMessage(&internal::ChannelTransport::VTable,
                         reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes, c_msg->handles,
                         c_msg->handle_metadata, c_msg->num_handles);
}

IncomingMessage::IncomingMessage(const internal::TransportVTable* transport_vtable, uint8_t* bytes,
                                 uint32_t byte_actual, zx_handle_t* handles,
                                 fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual,
                                 SkipMessageHeaderValidationTag)
    : fidl::Result(fidl::Result::Ok()),
      transport_vtable_(transport_vtable),
      message_{
          .bytes = bytes,
          .handles = handles,
          .handle_metadata = handle_metadata,
          .num_bytes = byte_actual,
          .num_handles = handle_actual,
      } {}

IncomingMessage::IncomingMessage(const fidl::Result& failure) : fidl::Result(failure), message_ {}
{ ZX_DEBUG_ASSERT(failure.status() != ZX_OK); }

IncomingMessage::~IncomingMessage() { std::move(*this).CloseHandles(); }

fidl_incoming_msg_t IncomingMessage::ReleaseToEncodedCMessage() && {
  ZX_DEBUG_ASSERT(status() == ZX_OK);
  ZX_ASSERT(transport_vtable_->type == FIDL_TRANSPORT_TYPE_CHANNEL);
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
  Decode(wire_format_version, message_type, true, out_transformed_buffer);
}

void IncomingMessage::Decode(internal::WireFormatVersion wire_format_version,
                             const fidl_type_t* message_type, bool is_transactional,
                             std::unique_ptr<uint8_t[]>* out_transformed_buffer) {
  ZX_DEBUG_ASSERT(status() == ZX_OK);

  if (wire_format_version == internal::WireFormatVersion::kV1) {
    uint8_t* trimmed_bytes = bytes();
    uint32_t trimmed_num_bytes = byte_actual();
    if (is_transactional) {
      zx_status_t status = ::fidl::internal::fidl_exclude_header_bytes(
          bytes(), byte_actual(), &trimmed_bytes, &trimmed_num_bytes, error_address());
      if (status != ZX_OK) {
        SetResult(fidl::Result::DecodeError(status, *error_address()));
        return;
      }
    }

    zx_status_t status = internal__fidl_validate__v1__may_break(
        message_type, trimmed_bytes, trimmed_num_bytes, handle_actual(), error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }

    if (message_type == nullptr ||
        !internal__fidl_tranform_is_noop__may_break(FIDL_TRANSFORMATION_V1_TO_V2, message_type)) {
      *out_transformed_buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
      uint32_t actual_num_bytes = 0;
      status = internal__fidl_transform__may_break(
          FIDL_TRANSFORMATION_V1_TO_V2, message_type, is_transactional, bytes(), byte_actual(),
          out_transformed_buffer->get(), ZX_CHANNEL_MAX_MSG_BYTES, &actual_num_bytes,
          error_address());
      if (status != ZX_OK) {
        SetResult(fidl::Result::DecodeError(status, *error_address()));
        return;
      }

      // Update this object with the transformed value.
      message_.bytes = out_transformed_buffer->get();
      message_.num_bytes = actual_num_bytes;
    }
  }

  uint8_t* trimmed_bytes = bytes();
  uint32_t trimmed_num_bytes = byte_actual();
  if (is_transactional) {
    zx_status_t status = ::fidl::internal::fidl_exclude_header_bytes(
        bytes(), byte_actual(), &trimmed_bytes, &trimmed_num_bytes, error_address());
    if (status != ZX_OK) {
      SetResult(fidl::Result::DecodeError(status, *error_address()));
      return;
    }
  }

  fidl_trace(WillLLCPPDecode, message_type, trimmed_bytes, trimmed_num_bytes, handle_actual());
  zx_status_t status = fidl::internal::DecodeEtc<FIDL_WIRE_FORMAT_VERSION_V2>(
      *transport_vtable_->encoding_configuration, message_type, trimmed_bytes, trimmed_num_bytes,
      message_.handles, message_.handle_metadata, message_.num_handles, error_address());
  fidl_trace(DidLLCPPDecode);
  // Now the caller is responsible for the handles contained in `bytes()`.
  ReleaseHandles();
  if (status != ZX_OK) {
    SetResult(fidl::Result::DecodeError(status, *error_address()));
  }
}

void IncomingMessage::ValidateHeader() {
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
  zx_handle_t* handles = input.handles();
  fidl_channel_handle_metadata_t* handle_metadata =
      input.handle_metadata<fidl::internal::ChannelTransport>();
  uint32_t num_handles = input.handle_actual();
  input.ReleaseHandles();

  if (num_handles > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FidlHandleCloseMany(handles, num_handles);
    return fidl::IncomingMessage::Create(fidl::Result::EncodeError(ZX_ERR_OUT_OF_RANGE));
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
      return fidl::IncomingMessage::Create(fidl::Result::EncodeError(status));
    }
    buf_handles[i] = handles[i];
    buf_handle_metadata[i] = handle_metadata[i];
  }

  buf_bytes = input.CopyBytes();
  if (buf_bytes.size() > ZX_CHANNEL_MAX_MSG_BYTES) {
    FidlHandleCloseMany(handles, num_handles);
    FidlHandleCloseMany(buf_handles.get(), num_handles);
    return fidl::IncomingMessage::Create(fidl::Result::EncodeError(ZX_ERR_INVALID_ARGS));
  }

  if (input.is_transactional()) {
    return fidl::IncomingMessage::Create(buf_bytes.data(), buf_bytes.size(), buf_handles.get(),
                                         buf_handle_metadata.get(), num_handles);
  }
  return fidl::IncomingMessage::Create(buf_bytes.data(), buf_bytes.size(), buf_handles.get(),
                                       buf_handle_metadata.get(), num_handles,
                                       fidl::IncomingMessage::kSkipMessageHeaderValidation);
}

}  // namespace fidl
