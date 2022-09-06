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
  // Convert the body message back to a transactional message to call |WireDecode|,
  // because |decode_fn| is a function that decodes both the header and the body.
  // TODO(fxbug.dev/82681): This function should be obsoleted by |fidl::InplaceDecode|.
  // To achieve this, we need to remove the transactional versions of
  // |fidl::unstable::DecodedMessage|. Instead, everyone should decode just the body.
  // This kludge will be removed as |IncomingHeaderAndMessage::Decode| goes away.
  fidl::EncodedMessage transactional_message = EncodedMessage{
      body_.transport_vtable(), bytes_, body_.handles(), body_.raw_handle_metadata(),
      body_.handle_actual(),
  };
  std::move(body_).ReleaseHandles();
  fidl::Status decode_status =
      internal::WireDecode(fidl::WireFormatMetadata::FromTransactionalHeader(*header()),
                           contains_envelope, inline_size, decode_fn, transactional_message);
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
