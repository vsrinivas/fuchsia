// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <zircon/fidl.h>

#include <cstdint>

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_OUTGOING_MESSAGE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_OUTGOING_MESSAGE_H_

namespace fidl_testing {

// Forward declaration of test helpers to support friend declaration.
class MessageChecker;

}  // namespace fidl_testing

namespace fidl {

namespace internal {

template <typename>
class UnownedEncodedMessageBase;

}

// |OutgoingMessage| represents a FIDL message on the write path.
//
// This class does not allocate its own memory storage. Instead, users need to
// pass in encoding buffers of sufficient size, which an |OutgoingMessage| will
// borrow until its destruction.
//
// This class takes ownership of handles in the message.
//
// For efficiency, errors are stored inside this object. |Write| operations are
// no-op and return the contained error if the message is in an error state.
class OutgoingMessage : public ::fidl::Status {
 public:
  // Copy and move is disabled for the sake of avoiding double handle close.
  // It is possible to implement the move operations with correct semantics if they are
  // ever needed.
  OutgoingMessage(const OutgoingMessage&) = delete;
  OutgoingMessage(OutgoingMessage&&) = delete;
  OutgoingMessage& operator=(const OutgoingMessage&) = delete;
  OutgoingMessage& operator=(OutgoingMessage&&) = delete;
  OutgoingMessage() = delete;
  ~OutgoingMessage();

  // Creates an object which can manage a FIDL message. This should only be used
  // when interfacing with C APIs. |c_msg| must contain an already-encoded
  // message. The handles in |c_msg| are owned by the returned |OutgoingMessage|
  // object.
  //
  // Only the channel transport is supported for C messages. For other transports,
  // use other constructors of |OutgoingMessage|.
  //
  // The bytes must represent a transactional message.
  static OutgoingMessage FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg);

  // Creates an object which can manage an encoded FIDL value.
  // This is identical to |FromEncodedCMessage| but the |OutgoingMessage|
  // is non-transactional instead of transactional.
  static OutgoingMessage FromEncodedCValue(const fidl_outgoing_msg_t* c_msg);

  struct InternalIovecConstructorArgs {
    const internal::TransportVTable* transport_vtable;
    zx_channel_iovec_t* iovecs;
    uint32_t iovec_capacity;
    fidl_handle_t* handles;
    fidl_handle_metadata_t* handle_metadata;
    uint32_t handle_capacity;
    uint8_t* backing_buffer;
    uint32_t backing_buffer_capacity;
    bool is_transactional;
  };
  // Creates an object which can manage a FIDL message.
  // |args.iovecs|, |args.handles| and |args.backing_buffer| contain undefined data that will be
  // populated during |Encode|.
  // Internal-only function that should not be called outside of the FIDL library.
  static OutgoingMessage Create_InternalMayBreak(InternalIovecConstructorArgs args) {
    return OutgoingMessage(args);
  }

  struct InternalByteBackedConstructorArgs {
    const internal::TransportVTable* transport_vtable;
    uint8_t* bytes;
    uint32_t num_bytes;
    fidl_handle_t* handles;
    fidl_handle_metadata_t* handle_metadata;
    uint32_t num_handles;
    bool is_transactional;
  };

  // Creates an object which can manage a FIDL message or body.
  // |args.bytes| and |args.handles| should already contain encoded data.
  // Internal-only function that should not be called outside of the FIDL library.
  static OutgoingMessage Create_InternalMayBreak(InternalByteBackedConstructorArgs args) {
    return OutgoingMessage(args);
  }

  // Creates an empty outgoing message representing an error.
  //
  // |failure| must contain an error result.
  explicit OutgoingMessage(const ::fidl::Status& failure);

  // Set the txid in the message header.
  //
  // Requires that the message is encoded, and is a transactional message.
  // Requires that there are sufficient bytes to store the header in the buffer.
  void set_txid(zx_txid_t txid) {
    if (!ok()) {
      return;
    }
    ZX_ASSERT(is_transactional_);
    ZX_ASSERT(iovec_actual() >= 1 && iovecs()[0].capacity >= sizeof(fidl_message_header_t));
    // The byte buffer is const because the kernel only reads the bytes.
    // const_cast is needed to populate it here.
    static_cast<fidl_message_header_t*>(const_cast<void*>(iovecs()[0].buffer))->txid = txid;
  }

  zx_channel_iovec_t* iovecs() const { return iovec_message().iovecs; }
  uint32_t iovec_actual() const { return iovec_message().num_iovecs; }
  fidl_handle_t* handles() const { return iovec_message().handles; }
  fidl_transport_type transport_type() const { return transport_vtable_->type; }
  uint32_t handle_actual() const { return iovec_message().num_handles; }

  template <typename Transport>
  typename Transport::HandleMetadata* handle_metadata() const {
    ZX_ASSERT(Transport::VTable.type == transport_vtable_->type);
    return reinterpret_cast<typename Transport::HandleMetadata*>(iovec_message().handle_metadata);
  }

  // Convert the outgoing message to its C API counterpart, releasing the
  // ownership of handles to the caller in the process. This consumes the
  // |OutgoingMessage|.
  //
  // This should only be called while the message is in its encoded form.
  fidl_outgoing_msg_t ReleaseToEncodedCMessage() &&;

  // Returns the number of bytes in the message.
  uint32_t CountBytes() const;

  // Returns true iff the bytes in this message are identical to the bytes in the argument.
  bool BytesMatch(const OutgoingMessage& other) const;

  // Holds a heap-allocated contiguous copy of the bytes in this message.
  //
  // This owns the allocated buffer and frees it when the object goes out of scope.
  // To create a |CopiedBytes|, use |CopyBytes|.
  class CopiedBytes {
   public:
    CopiedBytes() = default;
    CopiedBytes(CopiedBytes&&) = default;
    CopiedBytes& operator=(CopiedBytes&&) = default;
    CopiedBytes(const CopiedBytes&) = delete;
    CopiedBytes& operator=(const CopiedBytes&) = delete;

    uint8_t* data() { return bytes_.data(); }
    size_t size() const { return bytes_.size(); }

   private:
    explicit CopiedBytes(const OutgoingMessage& msg);

    std::vector<uint8_t> bytes_;

    friend class OutgoingMessage;
  };

  // Create a heap-allocated contiguous copy of the bytes in this message.
  CopiedBytes CopyBytes() const { return CopiedBytes(*this); }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { iovec_message().num_handles = 0; }

  // Writes the message to the |transport|.
  void Write(internal::AnyUnownedTransport transport, WriteOptions options = {});

  // Writes the message to the |transport|. This overload takes a concrete
  // transport endpoint, such as a |zx::unowned_channel|.
  template <typename TransportObject>
  void Write(TransportObject&& transport, WriteOptions options = {}) {
    Write(internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport)),
          std::move(options));
  }

  // Makes a call and returns the response read from the transport, without
  // decoding.
  template <typename TransportObject>
  auto Call(TransportObject&& transport,
            typename internal::AssociatedTransport<TransportObject>::MessageStorageView storage,
            CallOptions options = {}) {
    return CallImpl(internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport)),
                    static_cast<internal::MessageStorageViewBase&>(storage), std::move(options));
  }

  bool is_transactional() const { return is_transactional_; }

 private:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Status(::fidl::Status::Ok()), message_(msg), handle_capacity_(handle_capacity) {}

  void EncodeImpl(fidl::internal::WireFormatVersion wire_format_version, void* data,
                  size_t inline_size, fidl::internal::TopLevelEncodeFn encode_fn);

  uint32_t iovec_capacity() const { return iovec_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  uint32_t backing_buffer_capacity() const { return backing_buffer_capacity_; }
  uint8_t* backing_buffer() const { return backing_buffer_; }

  friend ::fidl_testing::MessageChecker;

  explicit OutgoingMessage(InternalIovecConstructorArgs args);
  explicit OutgoingMessage(InternalByteBackedConstructorArgs args);
  explicit OutgoingMessage(const fidl_outgoing_msg_t* msg, bool is_transactional);

  fidl::IncomingHeaderAndMessage CallImpl(internal::AnyUnownedTransport transport,
                                          internal::MessageStorageViewBase& storage,
                                          CallOptions options);

  fidl_outgoing_msg_iovec_t& iovec_message() {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }
  const fidl_outgoing_msg_iovec_t& iovec_message() const {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }

  using Status::SetStatus;

  const internal::TransportVTable* transport_vtable_ = nullptr;
  fidl_outgoing_msg_t message_ = {};
  uint32_t iovec_capacity_ = 0;
  uint32_t handle_capacity_ = 0;
  uint32_t backing_buffer_capacity_ = 0;
  uint8_t* backing_buffer_ = nullptr;

  // If OutgoingMessage is constructed with a fidl_outgoing_msg_t* that contains bytes
  // rather than iovec, it is converted to a single-element iovec pointing to the bytes.
  zx_channel_iovec_t converted_byte_message_iovec_ = {};
  bool is_transactional_ = false;

  template <typename>
  friend class internal::UnownedEncodedMessageBase;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_OUTGOING_MESSAGE_H_
