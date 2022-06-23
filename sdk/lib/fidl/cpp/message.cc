// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/trace.h>
#include <string.h>

#include "zircon/fidl.h"

#ifdef __Fuchsia__
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#endif

namespace {

bool ContainsEnvelope(const fidl_type_t* type) {
  switch (type->type_tag()) {
    case kFidlTypeTable:
    case kFidlTypeXUnion:
      return true;
    case kFidlTypeStruct:
      return type->coded_struct().contains_envelope;
    default:
      ZX_PANIC("unexpected top-level type");
  }
}

zx_status_t CheckWireFormatVersion(fidl::internal::WireFormatVersion wire_format,
                                   const fidl_type_t* type, const char** out_error_msg) {
  switch (wire_format) {
    case fidl::internal::WireFormatVersion::kV1:
      // Old versions of the C bindings will send wire format V1 payloads that are compatible
      // with wire format V2 (they don't contain envelopes). Confirm that V1 payloads don't
      // contain envelopes and are compatible with V2.
      if (ContainsEnvelope(type)) {
        *out_error_msg = "wire format v1 message received, but it is unsupported";
        return ZX_ERR_INVALID_ARGS;
      }
      return ZX_OK;
    case fidl::internal::WireFormatVersion::kV2:
      return ZX_OK;
    default:
      *out_error_msg = "unknown wire format version";
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace

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

zx_status_t HLCPPIncomingBody::Decode(const WireFormatMetadata& metadata, const fidl_type_t* type,
                                      const char** error_msg_out) {
  if (!metadata.is_valid()) {
    *error_msg_out = "invalid wire format metadata";
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = CheckWireFormatVersion(metadata.wire_format_version(), type, error_msg_out);
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
  zx_status_t status =
      body_view_.Decode(WireFormatMetadata::FromTransactionalHeader(header()), type, error_msg_out);
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

zx_status_t HLCPPOutgoingBody::Validate(const internal::WireFormatVersion& wire_format_version,
                                        const fidl_type_t* type, const char** error_msg_out) const {
  zx_status_t status = CheckWireFormatVersion(wire_format_version, type, error_msg_out);
  if (status != ZX_OK) {
    return status;
  }
  fidl_trace(WillHLCPPValidate, type, bytes_.data(), bytes_.actual(), handles().actual());
  status = internal__fidl_validate__v2__may_break(type, bytes_.data(), bytes_.actual(),
                                                  handles().actual(), error_msg_out);
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

zx_status_t HLCPPOutgoingMessage::Validate(const fidl_type_t* type,
                                           const char** error_msg_out) const {
  WireFormatMetadata wire_format_metadata = WireFormatMetadata::FromTransactionalHeader(header());
  if (!wire_format_metadata.is_valid()) {
    *error_msg_out = "invalid wire format metadata";
    return ZX_ERR_INVALID_ARGS;
  }
  return body_view_.Validate(wire_format_metadata.wire_format_version(), type, error_msg_out);
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
