// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/transformer.h>
#include <string.h>

#include "zircon/fidl.h"

#ifdef __Fuchsia__
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

HLCPPIncomingBody::HLCPPIncomingBody() = default;

HLCPPIncomingBody::HLCPPIncomingBody(BytePart bytes, HandleInfoPart handles)
    : bytes_(std::move(bytes)), handles_(std::move(handles)) {}

HLCPPIncomingBody::~HLCPPIncomingBody() {
#ifdef __Fuchsia__
  FidlHandleInfoCloseMany(handles_.data(), handles_.actual());
#endif
  ClearHandlesUnsafe();
}

HLCPPIncomingBody::HLCPPIncomingBody(HLCPPIncomingBody&& other)
    : bytes_(std::move(other.bytes_)), handles_(std::move(other.handles_)) {}

HLCPPIncomingBody& HLCPPIncomingBody::operator=(HLCPPIncomingBody&& other) {
  bytes_ = std::move(other.bytes_);
  handles_ = std::move(other.handles_);
  return *this;
}

zx_status_t HLCPPIncomingBody::Decode(const internal::WireFormatMetadata& metadata,
                                      const fidl_type_t* type, const char** error_msg_out) {
  uint32_t transformed_num_bytes = bytes_.actual();
  zx_status_t status = Transform(metadata, type, &transformed_num_bytes, error_msg_out);
  if (status != ZX_OK) {
    return status;
  }

  fidl_trace(WillHLCPPDecode, type, bytes_.data(), bytes_.actual(), handles_.actual());
  status = internal__fidl_decode_etc_hlcpp__v2__may_break(
      type, bytes_.data(), bytes_.actual(), handles_.data(), handles_.actual(), error_msg_out);
  fidl_trace(DidHLCPPDecode);

  ClearHandlesUnsafe();
  return status;
}

zx_status_t HLCPPIncomingBody::Transform(const internal::WireFormatMetadata& metadata,
                                         const fidl_type_t* type, uint32_t* new_num_bytes,
                                         const char** error_msg_out) {
  internal::WireFormatVersion version = metadata.wire_format_version();
  zx_status_t status = ZX_OK;
  switch (version) {
    case internal::WireFormatVersion::kV1: {
      if (type == nullptr) {
        break;
      }

      // Transform to V2.
      status = internal__fidl_validate__v1__may_break(type, bytes_.data(), bytes_.actual(),
                                                      handles_.actual(), error_msg_out);
      if (status != ZX_OK) {
        return status;
      }

      auto transformer_bytes = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
      uint32_t num_bytes = 0;
      status = internal__fidl_transform__may_break(
          FIDL_TRANSFORMATION_V1_TO_V2, type, false, bytes_.data(), bytes_.actual(),
          transformer_bytes.get(), ZX_CHANNEL_MAX_MSG_BYTES, &num_bytes, error_msg_out);
      if (status != ZX_OK) {
        return status;
      }

      if (num_bytes > bytes_.capacity()) {
        *error_msg_out = "transformed bytes exceeds message buffer capacity";
        return ZX_ERR_BUFFER_TOO_SMALL;
      }

      memcpy(bytes_.data(), transformer_bytes.get(), num_bytes);
      bytes_.set_actual(num_bytes);
      break;
    }
    case internal::WireFormatVersion::kV2:
      // No-op: the native format of HLCPP domain objects is V2.
      break;
  }
  return status;
}

void HLCPPIncomingBody::ClearHandlesUnsafe() { handles_.set_actual(0u); }

HLCPPIncomingMessage::HLCPPIncomingMessage() = default;

HLCPPIncomingMessage::HLCPPIncomingMessage(BytePart bytes, HandleInfoPart handles)
    : bytes_(std::move(bytes)),
      body_view_(
          HLCPPIncomingBody(BytePart(bytes_, sizeof(fidl_message_header_t)), std::move(handles))) {}

HLCPPIncomingMessage::HLCPPIncomingMessage(HLCPPIncomingMessage&& other)
    : bytes_(std::move(other.bytes_)), body_view_(std::move(other.body_view_)) {}

HLCPPIncomingMessage& HLCPPIncomingMessage::operator=(HLCPPIncomingMessage&& other) {
  bytes_ = std::move(other.bytes_);
  body_view_ = std::move(other.body_view_);
  return *this;
}

zx_status_t HLCPPIncomingMessage::Decode(const fidl_type_t* type, const char** error_msg_out) {
  zx_status_t status = body_view_.Decode(
      internal::WireFormatMetadata::FromTransactionalHeader(header()), type, error_msg_out);
  return status;
}

zx_status_t HLCPPIncomingMessage::Transform(const internal::WireFormatMetadata& metadata,
                                            const fidl_type_t* type, const char** error_msg_out) {
  uint32_t transformed_num_bytes = bytes_.actual();
  zx_status_t status = body_view_.Transform(metadata, type, &transformed_num_bytes, error_msg_out);
  if (status != ZX_OK) {
    return status;
  }

  bytes_.set_actual(transformed_num_bytes + sizeof(fidl_message_header_t));
  return status;
}

#ifdef __Fuchsia__
zx_status_t HLCPPIncomingMessage::Read(zx_handle_t channel, uint32_t flags) {
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  fidl_trace(WillHLCPPChannelRead);
  zx_status_t status =
      zx_channel_read_etc(channel, flags, bytes_.data(), handles().data(), bytes_.capacity(),
                          handles().capacity(), &actual_bytes, &actual_handles);
  if (status != ZX_OK) {
    return status;
  }
  fidl_trace(DidHLCPPChannelRead, nullptr /* type */, bytes_.data(), actual_bytes, actual_handles);

  // Ensure we received enough bytes for the FIDL header.
  if (actual_bytes < sizeof(fidl_message_header_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  resize_bytes(actual_bytes);
  handles().set_actual(actual_handles);
  return ZX_OK;
}
#endif

void HLCPPIncomingMessage::ClearHandlesUnsafe() { body_view_.ClearHandlesUnsafe(); }

HLCPPOutgoingBody::HLCPPOutgoingBody() = default;

HLCPPOutgoingBody::HLCPPOutgoingBody(BytePart bytes, HandleDispositionPart handles)
    : bytes_(std::move(bytes)), handles_(std::move(handles)) {}

HLCPPOutgoingBody::~HLCPPOutgoingBody() {
#ifdef __Fuchsia__
  if (handles_.actual() > 0) {
    FidlHandleDispositionCloseMany(handles_.data(), handles_.actual());
  }
#endif
  ClearHandlesUnsafe();
}

HLCPPOutgoingBody::HLCPPOutgoingBody(HLCPPOutgoingBody&& other)
    : bytes_(std::move(other.bytes_)), handles_(std::move(other.handles_)) {}

HLCPPOutgoingBody& HLCPPOutgoingBody::operator=(HLCPPOutgoingBody&& other) {
  bytes_ = std::move(other.bytes_);
  handles_ = std::move(other.handles_);
  return *this;
}

zx_status_t HLCPPOutgoingBody::Encode(const fidl_type_t* type, const char** error_msg_out) {
  uint32_t actual_handles = 0u;
  zx_status_t status = fidl_encode_etc(type, bytes_.data(), bytes_.actual(), handles().data(),
                                       handles().capacity(), &actual_handles, error_msg_out);
  if (status == ZX_OK)
    handles().set_actual(actual_handles);

  return status;
}

zx_status_t HLCPPOutgoingBody::Validate(const internal::WireFormatVersion& wire_format_version,
                                        const fidl_type_t* type, const char** error_msg_out) const {
  zx_status_t status;

  fidl_trace(WillHLCPPValidate, type, bytes_.data(), bytes_.actual(), handles().actual());
  switch (wire_format_version) {
    case internal::WireFormatVersion::kV1:
      status = internal__fidl_validate__v1__may_break(type, bytes_.data(), bytes_.actual(),
                                                      handles().actual(), error_msg_out);
      break;
    case internal::WireFormatVersion::kV2:
      status = internal__fidl_validate__v2__may_break(type, bytes_.data(), bytes_.actual(),
                                                      handles().actual(), error_msg_out);
      break;
    default:
      __builtin_unreachable();
  }
  fidl_trace(DidHLCPPValidate);

  return status;
}

void HLCPPOutgoingBody::ClearHandlesUnsafe() { handles_.set_actual(0u); }

HLCPPOutgoingMessage::HLCPPOutgoingMessage() = default;

HLCPPOutgoingMessage::HLCPPOutgoingMessage(BytePart bytes, HandleDispositionPart handles)
    : bytes_(std::move(bytes)),
      body_view_(
          HLCPPOutgoingBody(BytePart(bytes_, sizeof(fidl_message_header_t)), std::move(handles))) {}

HLCPPOutgoingMessage::HLCPPOutgoingMessage(HLCPPOutgoingMessage&& other)
    : bytes_(std::move(other.bytes_)), body_view_(std::move(other.body_view_)) {}

HLCPPOutgoingMessage& HLCPPOutgoingMessage::operator=(HLCPPOutgoingMessage&& other) {
  bytes_ = std::move(other.bytes_);
  body_view_ = std::move(other.body_view_);
  return *this;
}

zx_status_t HLCPPOutgoingMessage::Encode(const fidl_type_t* type, const char** error_msg_out) {
  uint8_t* trimmed_bytes = bytes_.data();
  uint32_t trimmed_num_bytes = bytes_.actual();
  zx_status_t status = ::fidl::internal::fidl_exclude_header_bytes(
      bytes_.data(), bytes_.actual(), &trimmed_bytes, &trimmed_num_bytes, error_msg_out);
  if (unlikely(status) != ZX_OK) {
    return status;
  }

  return body_view_.Encode(type, error_msg_out);
}

zx_status_t HLCPPOutgoingMessage::Validate(const fidl_type_t* type,
                                           const char** error_msg_out) const {
  internal::WireFormatVersion wire_format_version = internal::WireFormatVersion::kV1;
  if ((header().flags[0] & FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2) != 0) {
    wire_format_version = internal::WireFormatVersion::kV2;
  }

  return body_view_.Validate(wire_format_version, type, error_msg_out);
}

#ifdef __Fuchsia__
zx_status_t HLCPPOutgoingMessage::Write(zx_handle_t channel, uint32_t flags) {
  fidl_trace(WillHLCPPChannelWrite, nullptr /* type */, bytes_.data(), bytes_.actual(),
             handles().actual());
  zx_status_t status = zx_channel_write_etc(channel, flags, bytes_.data(), bytes_.actual(),
                                            handles().data(), handles().actual());
  fidl_trace(DidHLCPPChannelWrite);

  // Handles are cleared by the kernel on either success or failure.
  ClearHandlesUnsafe();

  return status;
}

zx_status_t HLCPPOutgoingMessage::Call(zx_handle_t channel, uint32_t flags, zx_time_t deadline,
                                       HLCPPIncomingMessage* response) {
  zx_channel_call_etc_args_t args;
  args.wr_bytes = bytes_.data();
  args.wr_handles = handles().data();
  args.rd_bytes = response->bytes().data();
  args.rd_handles = response->handles().data();
  args.wr_num_bytes = bytes_.actual();
  args.wr_num_handles = handles().actual();
  args.rd_num_bytes = response->bytes().capacity();
  args.rd_num_handles = response->handles().capacity();
  uint32_t actual_bytes = 0u;
  uint32_t actual_handles = 0u;
  zx_status_t status =
      zx_channel_call_etc(channel, flags, deadline, &args, &actual_bytes, &actual_handles);
  ClearHandlesUnsafe();
  if (status == ZX_OK) {
    response->resize_bytes(actual_bytes);
    response->handles().set_actual(actual_handles);
  }
  return status;
}
#endif

void HLCPPOutgoingMessage::ClearHandlesUnsafe() { body_view_.ClearHandlesUnsafe(); }

}  // namespace fidl
