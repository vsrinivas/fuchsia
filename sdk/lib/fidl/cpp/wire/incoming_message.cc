// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/txn_header.h>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
#endif  // __Fuchsia__

namespace fidl {

EncodedMessage EncodedMessage::Create(cpp20::span<uint8_t> bytes) {
  return EncodedMessage(nullptr, bytes, nullptr, nullptr, 0);
}

EncodedMessage EncodedMessage::Create(cpp20::span<uint8_t> bytes, zx_handle_t* handles,
                                      fidl_channel_handle_metadata_t* handle_metadata,
                                      uint32_t handle_actual) {
  return EncodedMessage(&internal::ChannelTransport::VTable, bytes, handles,
                        reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata), handle_actual);
}

EncodedMessage EncodedMessage::FromEncodedCMessage(const fidl_incoming_msg_t* c_msg) {
  return EncodedMessage(
      &internal::ChannelTransport::VTable,
      cpp20::span<uint8_t>{reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes},
      c_msg->handles, c_msg->handle_metadata, c_msg->num_handles);
}

fidl_incoming_msg_t EncodedMessage::ReleaseToEncodedCMessage() && {
  ZX_ASSERT(transport_vtable_->type == FIDL_TRANSPORT_TYPE_CHANNEL);
  fidl_incoming_msg_t result = message_;
  std::move(*this).ReleaseHandles();
  return result;
}

EncodedMessage::~EncodedMessage() { std::move(*this).CloseHandles(); }

void EncodedMessage::ReleaseHandles() && {
  message_.num_handles = 0;
  transport_vtable_ = nullptr;
}

void EncodedMessage::CloseHandles() && {
  if (transport_vtable_) {
    transport_vtable_->encoding_configuration->close_many(handles(), handle_actual());
  }
  std::move(*this).ReleaseHandles();
}

EncodedMessage::EncodedMessage(const internal::TransportVTable* transport_vtable,
                               cpp20::span<uint8_t> bytes, fidl_handle_t* handles,
                               fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual)
    : transport_vtable_(transport_vtable),
      message_(fidl_incoming_msg_t{
          .bytes = bytes.begin(),
          .handles = handles,
          .handle_metadata = handle_metadata,
          .num_bytes = static_cast<uint32_t>(bytes.size()),
          .num_handles = handle_actual,
      }) {
  ZX_DEBUG_ASSERT(bytes.size() < std::numeric_limits<uint32_t>::max());
}

void EncodedMessage::MoveImpl(EncodedMessage&& other) noexcept {
  transport_vtable_ = other.transport_vtable_;
  message_ = other.message_;
  std::move(other).ReleaseHandles();
}

IncomingHeaderAndMessage IncomingHeaderAndMessage::FromEncodedCMessage(
    const fidl_incoming_msg_t* c_msg) {
  ZX_DEBUG_ASSERT(c_msg->num_bytes >= sizeof(fidl_message_header_t));
  return IncomingHeaderAndMessage(&internal::ChannelTransport::VTable,
                                  reinterpret_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes,
                                  c_msg->handles, c_msg->handle_metadata, c_msg->num_handles);
}

IncomingHeaderAndMessage::~IncomingHeaderAndMessage() = default;

fidl_incoming_msg_t IncomingHeaderAndMessage::ReleaseToEncodedCMessage() && {
  ZX_DEBUG_ASSERT(status() == ZX_OK);
  fidl_incoming_msg_t msg = std::move(body_).ReleaseToEncodedCMessage();
  msg.bytes = bytes_.begin();
  msg.num_bytes = static_cast<uint32_t>(bytes_.size());
  return msg;
}

void IncomingHeaderAndMessage::CloseHandles() && { std::move(body_).CloseHandles(); }

EncodedMessage IncomingHeaderAndMessage::SkipTransactionHeader() && { return std::move(body_); }

IncomingHeaderAndMessage::IncomingHeaderAndMessage(const fidl::Status& failure)
    : fidl::Status(failure), body_(EncodedMessage::Create({})) {
  ZX_DEBUG_ASSERT(failure.status() != ZX_OK);
}

IncomingHeaderAndMessage::IncomingHeaderAndMessage(
    const internal::TransportVTable* transport_vtable, uint8_t* bytes, uint32_t byte_actual,
    fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual)
    : fidl::Status(fidl::Status::Ok()),
      bytes_(cpp20::span{bytes, byte_actual}),
      body_(EncodedMessage(transport_vtable, bytes_.subspan(sizeof(fidl_message_header_t)), handles,
                           handle_metadata, handle_actual)) {
  ValidateHeader();
}

void IncomingHeaderAndMessage::Decode(size_t inline_size, bool contains_envelope,
                                      internal::TopLevelDecodeFn decode_fn) {
  ZX_DEBUG_ASSERT(status() == ZX_OK);

  // Old versions of the C bindings will send wire format V1 payloads that are compatible
  // with wire format V2 (they don't contain envelopes). Confirm that V1 payloads don't
  // contain envelopes and are compatible with V2.
  // TODO(fxbug.dev/99738) Remove this logic.
  if ((header()->at_rest_flags[0] & FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2) == 0 &&
      contains_envelope) {
    SetStatus(fidl::Status::DecodeError(
        ZX_ERR_INVALID_ARGS, "wire format v1 header received with unsupported envelope"));
    return;
  }

  fidl::Status decode_status = internal::WireDecode(
      inline_size, decode_fn, body_.transport_vtable_->encoding_configuration, bytes(),
      byte_actual(), handles(), body_.raw_handle_metadata(), handle_actual());

  // Now the caller is responsible for the handles contained in `bytes()`.
  std::move(*this).ReleaseHandles();
  if (!decode_status.ok()) {
    SetStatus(decode_status);
  }
}

void IncomingHeaderAndMessage::ValidateHeader() {
  if (byte_actual() < sizeof(fidl_message_header_t)) {
    return SetStatus(fidl::Status::UnexpectedMessage(ZX_ERR_INVALID_ARGS,
                                                     ::fidl::internal::kErrorInvalidHeader));
  }

  auto* hdr = header();
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    return SetStatus(
        fidl::Status::UnexpectedMessage(status, ::fidl::internal::kErrorInvalidHeader));
  }

  // See
  // https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0053_epitaphs?hl=en#wire_format
  if (unlikely(maybe_epitaph())) {
    if (hdr->txid != 0) {
      return SetStatus(fidl::Status::UnexpectedMessage(ZX_ERR_INVALID_ARGS,
                                                       ::fidl::internal::kErrorInvalidHeader));
    }
  }
}

}  // namespace fidl
