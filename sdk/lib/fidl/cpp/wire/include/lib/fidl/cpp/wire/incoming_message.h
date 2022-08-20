// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INCOMING_MESSAGE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INCOMING_MESSAGE_H_

#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/stdcompat/span.h>
#include <zircon/fidl.h>

namespace fidl {

template <typename FidlType>
class DecodedValue;

namespace internal {

class DecodedMessageBase;
class NaturalDecoder;

}  // namespace internal

// |EncodedMessage| represents an encoded FIDL message consisting of some
// contiguous bytes and handles. See
// https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#message
//
// |EncodedMessage| does not own the storage behind the bytes and handles. But
// handles are owned by it and closed when the |EncodedMessage| is destroyed.
class EncodedMessage {
 public:
  // Creates an |EncodedMessage| consisting of only |bytes| and no handles.
  static EncodedMessage Create(cpp20::span<uint8_t> bytes);

  // Creates an |EncodedMessage| representing a message received from Zircon
  // channels.
  //
  // |handle_metadata| should point to an array with the same length as
  // |handles|. Each member in |handle_metadata| describes the type and rights
  // associated with the corresponding handle. This information is typically
  // obtained from the array of |zx_handle_info_t| coming from a channel read.
  static EncodedMessage Create(cpp20::span<uint8_t> bytes, zx_handle_t* handles,
                               fidl_channel_handle_metadata_t* handle_metadata,
                               uint32_t handle_actual);

  // Creates an |EncodedMessage| which manages a FIDL message from a custom
  // transport.
  //
  // |Transport| should be a type that represents a FIDL transport.
  // |HandleMetadata| is the type of metadata associated with handles being sent
  // over that transport.
  //
  // This function is generally reserved for internal use. Transport
  // implementations should offer non-templated free functions that creates
  // |EncodedMessage| given handle metadata specific to that transport.
  template <typename Transport>
  static EncodedMessage Create(cpp20::span<uint8_t> bytes, fidl_handle_t* handles,
                               typename Transport::HandleMetadata* handle_metadata,
                               uint32_t handle_actual) {
    return EncodedMessage(&Transport::VTable, bytes, handles,
                          reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
                          handle_actual);
  }

  // Creates an |EncodedMessage| from a C |fidl_incoming_msg_t| already in
  // encoded form. This should only be used when interfacing with C APIs. The
  // handles in |c_msg| are owned by the returned |EncodedMessage| object.
  //
  // The bytes must represent a regular FIDL message instead of a transactional
  // message. To adopt a transactional message, see
  // |IncomingHeaderAndMessage::FromEncodedCMessage|.
  static EncodedMessage FromEncodedCMessage(const fidl_incoming_msg_t* c_msg);

  // Convert the incoming message to its C API counterpart, releasing the
  // ownership of handles to the caller in the process. This consumes the
  // |EncodedMessage|.
  fidl_incoming_msg_t ReleaseToEncodedCMessage() &&;

  EncodedMessage(const EncodedMessage&) = delete;
  EncodedMessage& operator=(const EncodedMessage&) = delete;

  EncodedMessage(EncodedMessage&& other) noexcept { MoveImpl(std::move(other)); }
  EncodedMessage& operator=(EncodedMessage&& other) noexcept {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  ~EncodedMessage();

  cpp20::span<uint8_t> bytes() const {
    return {reinterpret_cast<uint8_t*>(message_.bytes), message_.num_bytes};
  }

  fidl_handle_t* handles() const { return message_.handles; }
  uint32_t handle_actual() const { return message_.num_handles; }
  fidl_handle_metadata_t* raw_handle_metadata() const { return message_.handle_metadata; }

  template <typename Transport>
  typename Transport::HandleMetadata* handle_metadata() const {
    ZX_ASSERT(Transport::VTable.type == transport_vtable_->type);
    return reinterpret_cast<typename Transport::HandleMetadata*>(message_.handle_metadata);
  }

  // Release the handle ownership after the message has been converted to its
  // decoded form. When used standalone and not as part of a decode, this method
  // is only useful when interfacing with C APIs.
  //
  // This consumes the |EncodedMessage|.
  void ReleaseHandles() &&;

  // Closes the handles managed by this message. This may be used when the code
  // would like to consume a |EncodedMessage&&| and close its handles, but does
  // not want to incur the overhead of moving it into a regular
  // |EncodedMessage| object, and running the destructor.
  //
  // This consumes the |EncodedMessage|.
  void CloseHandles() &&;

 private:
  friend class IncomingHeaderAndMessage;
  friend class internal::NaturalDecoder;
  template <typename FidlType>
  friend ::fitx::result<::fidl::Error, ::fidl::DecodedValue<FidlType>> InplaceDecode(
      fidl::EncodedMessage message, WireFormatMetadata metadata);

  const internal::TransportVTable* transport_vtable() const { return transport_vtable_; }

  EncodedMessage(const internal::TransportVTable* transport_vtable, cpp20::span<uint8_t> bytes,
                 fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata,
                 uint32_t handle_actual);

  void MoveImpl(EncodedMessage&& other) noexcept;

  const internal::TransportVTable* transport_vtable_ = nullptr;
  fidl_incoming_msg_t message_;
};

// |IncomingHeaderAndMessage| represents a FIDL transactional message on the
// read path. Transactional messages are a message header followed by a a
// regular message. See
// https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
//
// Each instantiation of the class should only be used for one message.
//
// |IncomingHeaderAndMessage|s are created with the results from reading
// from a channel. It automatically performs necessary validation on the message
// header.
//
// |IncomingHeaderAndMessage| relinquishes the ownership of the handles
// after decoding. Instead, callers must adopt the decoded content into another
// RAII class, such as |fidl::unstable::DecodedMessage<FidlType>|.
//
// Functions that take |IncomingHeaderAndMessage&| conditionally take
// ownership of the message. For functions in the public API, they must then
// indicate through their return value if they took ownership. For functions in
// the binding internals, it is sufficient to only document the conditions where
// minimum overhead is desired.
//
// Functions that take |IncomingHeaderAndMessage&&| always take ownership of
// the message. In practice, this means that they must either decode the
// message, or close the handles, or move the message into a deeper function
// that takes |IncomingHeaderAndMessage&&|.
//
// For efficiency, errors are stored inside this object. Callers must check for
// errors after construction, and after performing each operation on the object.
//
// An |IncomingHeaderAndMessage| may be created from |fidl::MessageRead|:
//
//     // Read a transactional message from a Zircon channel.
//     fidl::IncomingHeaderAndMessage message = fidl::MessageRead(
//         zx::unowned_channel(...),
//         fidl::ChannelMessageStorageView{...});
//
//    if (!msg.ok()) { /* ... error handling ... */ }
//
class IncomingHeaderAndMessage : public ::fidl::Status {
 public:
  // Creates an object which can manage a FIDL message. Allocated memory is not
  // owned by the |IncomingHeaderAndMessage|, but handles are owned by it and
  // cleaned up when the |IncomingHeaderAndMessage| is destructed.
  //
  // The bytes must represent a transactional message. See
  // https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
  template <typename HandleMetadata>
  static IncomingHeaderAndMessage Create(uint8_t* bytes, uint32_t byte_actual,
                                         fidl_handle_t* handles, HandleMetadata* handle_metadata,
                                         uint32_t handle_actual) {
    return Create<typename internal::AssociatedTransport<HandleMetadata>>(
        bytes, byte_actual, handles, handle_metadata, handle_actual);
  }

  // Creates an object which can manage a FIDL message. Allocated memory is not
  // owned by the |IncomingHeaderAndMessage|, but handles are owned by it and
  // cleaned up when the |IncomingHeaderAndMessage| is destructed.
  //
  // The bytes must represent a transactional message. See
  // https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
  template <typename Transport>
  static IncomingHeaderAndMessage Create(uint8_t* bytes, uint32_t byte_actual,
                                         fidl_handle_t* handles,
                                         typename Transport::HandleMetadata* handle_metadata,
                                         uint32_t handle_actual) {
    return IncomingHeaderAndMessage(&Transport::VTable, bytes, byte_actual, handles,
                                    reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
                                    handle_actual);
  }

  // Creates an |IncomingHeaderAndMessage| from a C |fidl_incoming_msg_t| already in
  // encoded form. This should only be used when interfacing with C APIs.
  // The handles in |c_msg| are owned by the returned |IncomingHeaderAndMessage| object.
  //
  // The bytes must represent a transactional message.
  static IncomingHeaderAndMessage FromEncodedCMessage(const fidl_incoming_msg_t* c_msg);

  // Creates an empty incoming message representing an error (e.g. failed to read from
  // a channel).
  //
  // |failure| must contain an error result.
  static IncomingHeaderAndMessage Create(const ::fidl::Status& failure) {
    return IncomingHeaderAndMessage(failure);
  }

  IncomingHeaderAndMessage(const IncomingHeaderAndMessage&) = delete;
  IncomingHeaderAndMessage& operator=(const IncomingHeaderAndMessage&) = delete;

  IncomingHeaderAndMessage(IncomingHeaderAndMessage&& other) noexcept
      : ::fidl::Status(other), body_(EncodedMessage::Create({})) {
    MoveImpl(std::move(other));
  }
  IncomingHeaderAndMessage& operator=(IncomingHeaderAndMessage&& other) noexcept {
    ::fidl::Status::operator=(other);
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  ~IncomingHeaderAndMessage();

  fidl_message_header_t* header() const {
    ZX_DEBUG_ASSERT(ok());
    return reinterpret_cast<fidl_message_header_t*>(bytes());
  }

  // If the message is an epitaph, returns a pointer to the epitaph structure.
  // Otherwise, returns null.
  fit::nullable<fidl_epitaph_t*> maybe_epitaph() const {
    ZX_DEBUG_ASSERT(ok());
    if (unlikely(header()->ordinal == kFidlOrdinalEpitaph)) {
      return fit::nullable(reinterpret_cast<fidl_epitaph_t*>(bytes()));
    }
    return fit::nullable<fidl_epitaph_t*>{};
  }

  uint8_t* bytes() const { return bytes_.begin(); }
  uint32_t byte_actual() const { return static_cast<uint32_t>(bytes_.size()); }

  fidl_handle_t* handles() const { return body_.handles(); }
  uint32_t handle_actual() const { return body_.handle_actual(); }
  fidl_handle_metadata_t* raw_handle_metadata() const { return body_.raw_handle_metadata(); }

  template <typename Transport>
  typename Transport::HandleMetadata* handle_metadata() const {
    return body_.handle_metadata<Transport>();
  }

  // Convert the incoming message to its C API counterpart, releasing the
  // ownership of handles to the caller in the process. This consumes the
  // |IncomingHeaderAndMessage|.
  //
  // This should only be called while the message is in its encoded form.
  fidl_incoming_msg_t ReleaseToEncodedCMessage() &&;

  // Closes the handles managed by this message. This may be used when the
  // code would like to consume a |IncomingHeaderAndMessage&&| and close its handles,
  // but does not want to incur the overhead of moving it into a regular
  // |IncomingHeaderAndMessage| object, and running the destructor.
  //
  // This consumes the |IncomingHeaderAndMessage|.
  void CloseHandles() &&;

  // Consumes self and returns an |EncodedMessage| with the transaction
  // header bytes skipped.
  EncodedMessage SkipTransactionHeader() &&;

 private:
  explicit IncomingHeaderAndMessage(const ::fidl::Status& failure);
  IncomingHeaderAndMessage(const internal::TransportVTable* transport_vtable, uint8_t* bytes,
                           uint32_t byte_actual, fidl_handle_t* handles,
                           fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual);

  // Only |fidl::unstable::DecodedMessage<T>| instances may decode this message.
  friend class internal::DecodedMessageBase;
  friend class internal::NaturalDecoder;

  const internal::TransportVTable* transport_vtable() const { return body_.transport_vtable_; }

  // |OutgoingMessage| may create an |IncomingHeaderAndMessage| with a dynamic transport during
  // a call.
  friend class OutgoingMessage;

  // |MessageRead| may create an |IncomingHeaderAndMessage| with a dynamic transport after a
  // read.
  template <typename TransportObject>
  friend IncomingHeaderAndMessage MessageRead(
      TransportObject&& transport,
      typename internal::AssociatedTransport<TransportObject>::MessageStorageView storage,
      const ReadOptions& options);

  // Release the handle ownership after the message has been converted to its
  // decoded form. When used standalone and not as part of a |Decode|, this
  // method is only useful when interfacing with C APIs.
  void ReleaseHandles() && { std::move(body_).ReleaseHandles(); }

  void MoveImpl(IncomingHeaderAndMessage&& other) noexcept {
    bytes_ = other.bytes_;
    body_ = std::move(other.body_);
    std::move(other).ReleaseHandles();
  }

  // Decodes the message using |decode_fn|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  //
  // The first 16 bytes of the message must be the FIDL message header and are used for
  // determining the wire format version for decoding.
  //
  // On success, the handles owned by |IncomingHeaderAndMessage| are transferred to the decoded
  // bytes.
  //
  // This method should be used after a read.
  // TODO(fxbug.dev/82681): This should be obsoleted by |fidl::InplaceDecode|.
  // To achieve this, we need to remove the transactional versions of
  // |fidl::unstable::DecodedMessage|. Instead, everyone should decode just the body.
  void Decode(size_t inline_size, bool contains_envelope, internal::TopLevelDecodeFn decode_fn);

  // Performs basic transactional message header validation and sets the |fidl::Status| fields
  // accordingly.
  void ValidateHeader();

  // The byte sequence covering the header message followed by an optional body message.
  cpp20::span<uint8_t> bytes_;

  // The body message.
  EncodedMessage body_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INCOMING_MESSAGE_H_
