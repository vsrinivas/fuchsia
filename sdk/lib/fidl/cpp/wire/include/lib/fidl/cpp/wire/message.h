// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_H_

#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/decoded_value.h>
#include <lib/fidl/cpp/wire/incoming_message.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire/wire_types.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/nullable.h>
#include <lib/fit/result.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

namespace fidl_testing {
// Forward declaration of test helpers to support friend declaration.
class MessageChecker;
}  // namespace fidl_testing

namespace fidl {

namespace internal {

constexpr WireFormatVersion kLLCPPWireFormatVersion = WireFormatVersion::kV2;

// Marker to allow references/pointers to the unowned input objects in OwnedEncodedMessage.
// This enables iovec optimizations but requires the input objects to stay in scope until the
// encoded result has been consumed.
struct AllowUnownedInputRef {};

template <typename Transport>
class UnownedEncodedMessageBase;

}  // namespace internal

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

  // Encodes the data.
  template <typename FidlType>
  void Encode(FidlType* data) {
    Encode(fidl::internal::WireFormatVersion::kV2, data);
  }

  template <typename FidlType>
  void Encode(fidl::internal::WireFormatVersion wire_format_version, FidlType* data) {
    is_transactional_ = fidl::IsFidlTransactionalMessage<FidlType>::value;

    EncodeImpl(wire_format_version, data, internal::TopLevelCodingTraits<FidlType>::inline_size,
               internal::MakeTopLevelEncodeFn<FidlType>());
  }

  // Various helper functions for writing to other channel-like types.

  void Write(internal::AnyUnownedTransport transport, WriteOptions options = {});

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

 protected:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Status(::fidl::Status::Ok()), message_(msg), handle_capacity_(handle_capacity) {}

  void EncodeImpl(fidl::internal::WireFormatVersion wire_format_version, void* data,
                  size_t inline_size, fidl::internal::TopLevelEncodeFn encode_fn);

  uint32_t iovec_capacity() const { return iovec_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  uint32_t backing_buffer_capacity() const { return backing_buffer_capacity_; }
  uint8_t* backing_buffer() const { return backing_buffer_; }

 private:
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

// Reads a transactional message from |transport| using the |storage| as needed.
//
// |storage| must be a subclass of |fidl::internal::MessageStorageViewBase|, and
// is specific to the transport. For example, the Zircon channel transport uses
// |fidl::ChannelMessageStorageView| which points to bytes and handles:
//
//     fidl::IncomingHeaderAndMessage message = fidl::MessageRead(
//         zx::unowned_channel(...),
//         fidl::ChannelMessageStorageView{...});
//
// Error information is embedded in the returned |IncomingHeaderAndMessage| in case of
// failures.
template <typename TransportObject>
IncomingHeaderAndMessage MessageRead(
    TransportObject&& transport,
    typename internal::AssociatedTransport<TransportObject>::MessageStorageView storage,
    const ReadOptions& options) {
  auto type_erased_transport =
      internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport));
  uint8_t* result_bytes;
  fidl_handle_t* result_handles;
  fidl_handle_metadata_t* result_handle_metadata;
  uint32_t actual_num_bytes = 0u;
  uint32_t actual_num_handles = 0u;
  zx_status_t status =
      type_erased_transport.read(options, internal::ReadArgs{
                                              .storage_view = &storage,
                                              .out_data = reinterpret_cast<void**>(&result_bytes),
                                              .out_handles = &result_handles,
                                              .out_handle_metadata = &result_handle_metadata,
                                              .out_data_actual_count = &actual_num_bytes,
                                              .out_handles_actual_count = &actual_num_handles,
                                          });
  if (status != ZX_OK) {
    return IncomingHeaderAndMessage::Create(fidl::Status::TransportError(status));
  }
  return IncomingHeaderAndMessage(type_erased_transport.vtable(), result_bytes, actual_num_bytes,
                                  result_handles, result_handle_metadata, actual_num_handles);
}

// Overload of |MessageRead| with default options. See other |MessageRead|.
template <typename TransportObject>
IncomingHeaderAndMessage MessageRead(
    TransportObject&& transport,
    typename internal::AssociatedTransport<TransportObject>::MessageStorageView storage) {
  return MessageRead(std::forward<TransportObject>(transport), storage, {});
}

namespace internal {

// This type exists because of class initialization order.
// If these are members of UnownedEncodedMessage, they will be initialized before
// UnownedEncodedMessageBase
template <typename FidlType, typename Transport>
struct UnownedEncodedMessageHandleContainer {
 protected:
  static constexpr uint32_t kNumHandles =
      fidl::internal::ClampedHandleCount<FidlType, fidl::MessageDirection::kSending>();
  std::array<zx_handle_t, kNumHandles> handle_storage_;
  std::array<typename Transport::HandleMetadata, kNumHandles> handle_metadata_storage_;
};

template <typename Transport>
class UnownedEncodedMessageBase {
 public:
  zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
  const char* status_string() const { return message_.status_string(); }
#endif
  bool ok() const { return message_.status() == ZX_OK; }
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Status& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

  ::fidl::WireFormatMetadata wire_format_metadata() const {
    return fidl::internal::WireFormatMetadataForVersion(wire_format_version_);
  }

  template <typename TransportObject>
  void Write(TransportObject&& client, WriteOptions options = {}) {
    message_.Write(std::forward<TransportObject>(client), std::move(options));
  }

 protected:
  UnownedEncodedMessageBase(::fidl::internal::WireFormatVersion wire_format_version,
                            uint32_t iovec_capacity,
                            ::fit::result<::fidl::Error, ::fidl::BufferSpan> backing_buffer,
                            fidl_handle_t* handles, fidl_handle_metadata_t* handle_metadata,
                            uint32_t handle_capacity, bool is_transactional, void* value,
                            size_t inline_size, TopLevelEncodeFn encode_fn)
      : message_(backing_buffer.is_ok()
                     ? ::fidl::OutgoingMessage::Create_InternalMayBreak(
                           ::fidl::OutgoingMessage::InternalIovecConstructorArgs{
                               .transport_vtable = &Transport::VTable,
                               .iovecs = iovecs_,
                               .iovec_capacity = iovec_capacity,
                               .handles = handles,
                               .handle_metadata = handle_metadata,
                               .handle_capacity = handle_capacity,
                               .backing_buffer = backing_buffer->data,
                               .backing_buffer_capacity = backing_buffer->capacity,
                               .is_transactional = is_transactional,
                           })
                     : ::fidl::OutgoingMessage{backing_buffer.error_value()}),
        wire_format_version_(wire_format_version) {
    if (message_.ok()) {
      ZX_ASSERT(iovec_capacity <= std::size(iovecs_));
      message_.EncodeImpl(wire_format_version, value, inline_size, encode_fn);
    }
  }

  UnownedEncodedMessageBase(const UnownedEncodedMessageBase&) = delete;
  UnownedEncodedMessageBase(UnownedEncodedMessageBase&&) = delete;
  UnownedEncodedMessageBase* operator=(const UnownedEncodedMessageBase&) = delete;
  UnownedEncodedMessageBase* operator=(UnownedEncodedMessageBase&&) = delete;

 private:
  zx_channel_iovec_t iovecs_[Transport::kNumIovecs];
  fidl::OutgoingMessage message_;
  fidl::internal::WireFormatVersion wire_format_version_;
};

}  // namespace internal

// TODO(fxbug.dev/82681): Re-introduce stable APIs for standalone use of the
// FIDL wire format.
namespace unstable {

// This class manages the handles within |FidlType| and encodes the message automatically upon
// construction. Different from |OwnedEncodedMessage|, it takes in a caller-allocated buffer and
// uses that as the backing storage for the message. The buffer must outlive instances of this
// class.
template <typename FidlType, typename Transport = internal::ChannelTransport>
class UnownedEncodedMessage final
    : public fidl::internal::UnownedEncodedMessageHandleContainer<FidlType, Transport>,
      public fidl::internal::UnownedEncodedMessageBase<Transport> {
  using UnownedEncodedMessageHandleContainer =
      fidl::internal::UnownedEncodedMessageHandleContainer<FidlType, Transport>;
  using UnownedEncodedMessageBase = ::fidl::internal::UnownedEncodedMessageBase<Transport>;

 public:
  UnownedEncodedMessage(uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(Transport::kNumIovecs, backing_buffer, backing_buffer_size,
                              response) {}
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(wire_format_version, Transport::kNumIovecs, backing_buffer,
                              backing_buffer_size, response) {}
  UnownedEncodedMessage(uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(fidl::internal::kLLCPPWireFormatVersion, iovec_capacity,
                              backing_buffer, backing_buffer_size, response) {}

  // Encodes |value| by allocating a backing buffer from |backing_buffer_allocator|.
  UnownedEncodedMessage(fidl::internal::AnyBufferAllocator& backing_buffer_allocator,
                        uint32_t backing_buffer_size, FidlType* value)
      : UnownedEncodedMessage(::fidl::internal::kLLCPPWireFormatVersion, Transport::kNumIovecs,
                              backing_buffer_allocator.TryAllocate(backing_buffer_size), value) {}

  // Encodes |value| using an existing |backing_buffer|.
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* value)
      : UnownedEncodedMessage(wire_format_version, iovec_capacity,
                              ::fit::ok(::fidl::BufferSpan(backing_buffer, backing_buffer_size)),
                              value) {}

  // Core implementation which other constructors delegate to.
  UnownedEncodedMessage(::fidl::internal::WireFormatVersion wire_format_version,
                        uint32_t iovec_capacity,
                        ::fit::result<::fidl::Error, ::fidl::BufferSpan> backing_buffer,
                        FidlType* value)
      : UnownedEncodedMessageBase(
            wire_format_version, iovec_capacity, backing_buffer,
            UnownedEncodedMessageHandleContainer::handle_storage_.data(),
            reinterpret_cast<fidl_handle_metadata_t*>(
                UnownedEncodedMessageHandleContainer::handle_metadata_storage_.data()),
            UnownedEncodedMessageHandleContainer::kNumHandles,
            fidl::IsFidlTransactionalMessage<FidlType>::value, value,
            internal::TopLevelCodingTraits<FidlType>::inline_size,
            internal::MakeTopLevelEncodeFn<FidlType>()) {}

  UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
  UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;
};

// This class owns a message of |FidlType| and encodes the message automatically upon construction
// into a byte buffer.
template <typename FidlType, typename Transport = internal::ChannelTransport>
class OwnedEncodedMessage final {
 public:
  explicit OwnedEncodedMessage(FidlType* response)
      : message_(1u, backing_buffer_.data(), static_cast<uint32_t>(backing_buffer_.size()),
                 response) {}
  explicit OwnedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                               FidlType* response)
      : message_(wire_format_version, 1u, backing_buffer_.data(),
                 static_cast<uint32_t>(backing_buffer_.size()), response) {}
  // Internal constructor.
  explicit OwnedEncodedMessage(::fidl::internal::AllowUnownedInputRef allow_unowned,
                               FidlType* response)
      : message_(Transport::kNumIovecs, backing_buffer_.data(),
                 static_cast<uint32_t>(backing_buffer_.size()), response) {}
  explicit OwnedEncodedMessage(::fidl::internal::AllowUnownedInputRef allow_unowned,
                               fidl::internal::WireFormatVersion wire_format_version,
                               FidlType* response)
      : message_(wire_format_version, Transport::kNumIovecs, backing_buffer_.data(),
                 static_cast<uint32_t>(backing_buffer_.size()), response) {}
  OwnedEncodedMessage(const OwnedEncodedMessage&) = delete;
  OwnedEncodedMessage(OwnedEncodedMessage&&) = delete;
  OwnedEncodedMessage* operator=(const OwnedEncodedMessage&) = delete;
  OwnedEncodedMessage* operator=(OwnedEncodedMessage&&) = delete;

  zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
  const char* status_string() const { return message_.status_string(); }
#endif
  bool ok() const { return message_.ok(); }
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Status& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

  template <typename TransportObject>
  void Write(TransportObject&& client, WriteOptions options = {}) {
    message_.Write(std::forward<TransportObject>(client), std::move(options));
  }

  ::fidl::WireFormatMetadata wire_format_metadata() const {
    return message_.wire_format_metadata();
  }

 private:
  ::fidl::internal::OutgoingMessageBuffer<FidlType> backing_buffer_;
  ::fidl::unstable::UnownedEncodedMessage<FidlType, Transport> message_;
};

// This class manages the handles within |FidlType| and decodes the message automatically upon
// construction. It always borrows external buffers for the backing storage of the message.
// This class should mostly be used for tests.
template <typename FidlType, typename Transport = internal::ChannelTransport,
          typename Enable = void>
class DecodedMessage;

// Specialization for non-transactional types (tables, structs, unions).
// TODO(fxbug.dev/82681): This should be obsoleted by |fidl::InplaceDecode|.
template <typename FidlType, typename Transport>
class DecodedMessage<FidlType, Transport,
                     std::enable_if_t<::fidl::IsFidlObject<FidlType>::value, void>>
    : public ::fidl::Status {
  static_assert(!IsFidlTransactionalMessage<FidlType>::value);

 public:
  DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles = nullptr,
                 typename Transport::HandleMetadata* handle_metadata = nullptr,
                 uint32_t handle_actual = 0)
      : DecodedMessage(::fidl::internal::WireFormatVersion::kV2, bytes, byte_actual, handles,
                       handle_metadata, handle_actual) {}

  // Internal constructor for specifying a specific wire format version.
  DecodedMessage(::fidl::internal::WireFormatVersion wire_format_version, uint8_t* bytes,
                 uint32_t byte_actual, zx_handle_t* handles = nullptr,
                 typename Transport::HandleMetadata* handle_metadata = nullptr,
                 uint32_t handle_actual = 0)
      : DecodedMessage(wire_format_version, ::fidl::EncodedMessage::Create<Transport>(
                                                cpp20::span<uint8_t>{bytes, byte_actual}, handles,
                                                handle_metadata, handle_actual)) {}

  DecodedMessage(internal::WireFormatVersion wire_format_version, ::fidl::EncodedMessage&& msg)
      : Status(::fidl::Status::Ok()) {
    ::fit::result result = ::fidl::InplaceDecode<FidlType>(
        std::move(msg), ::fidl::internal::WireFormatMetadataForVersion(wire_format_version));
    if (result.is_error()) {
      Status::operator=(result.error_value());
      return;
    }
    value_ = std::move(result.value());
  }

  explicit DecodedMessage(const fidl_incoming_msg_t* c_msg)
      : DecodedMessage(static_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes, c_msg->handles,
                       reinterpret_cast<fidl_channel_handle_metadata_t*>(c_msg->handle_metadata),
                       c_msg->num_handles) {}

  // Internal constructor for specifying a specific wire format version.
  DecodedMessage(::fidl::internal::WireFormatVersion wire_format_version,
                 const fidl_incoming_msg_t* c_msg)
      : DecodedMessage(wire_format_version, static_cast<uint8_t*>(c_msg->bytes), c_msg->num_bytes,
                       c_msg->handles,
                       reinterpret_cast<fidl_channel_handle_metadata_t*>(c_msg->handle_metadata),
                       c_msg->num_handles) {}

  ~DecodedMessage() = default;

  FidlType* PrimaryObject() {
    ZX_DEBUG_ASSERT(ok());
    return value_.pointer();
  }

  // Release the ownership of the decoded message. That means that the handles won't be closed
  // When the object is destroyed.
  // After calling this method, the |DecodedMessage| object should not be used anymore.
  void ReleasePrimaryObject() { value_.Release(); }

  ::fidl::DecodedValue<FidlType> Take() {
    ZX_ASSERT(ok());
    FidlType* value = PrimaryObject();
    ReleasePrimaryObject();
    return ::fidl::DecodedValue<FidlType>(value);
  }

 private:
  ::fidl::DecodedValue<FidlType> value_;
};

}  // namespace unstable

// Holds the result of converting an outgoing message to an incoming message.
//
// |OutgoingToIncomingMessage| objects own the bytes and handles resulting from
// conversion.
// TODO(fxbug.dev/107222): Rename to |OutgoingToEncodedMessage|.
class OutgoingToIncomingMessage {
 public:
  // Converts an outgoing message to an incoming message.
  //
  // The provided |OutgoingMessage| must use the Zircon channel transport.
  // It also must be a non-transactional outgoing message (i.e. from standalone
  // encoding and not from writing a request/response).
  //
  // In doing so, this function will make syscalls to fetch rights and type
  // information of any provided handles. The caller is responsible for ensuring
  // that returned handle rights and object types are checked appropriately.
  //
  // The constructed |OutgoingToIncomingMessage| will take ownership over
  // handles from the input |OutgoingMessage|.
  explicit OutgoingToIncomingMessage(OutgoingMessage& input);

  ~OutgoingToIncomingMessage() = default;

  fidl::EncodedMessage& incoming_message() & {
    ZX_DEBUG_ASSERT(ok());
    return incoming_message_;
  }

  [[nodiscard]] fidl::Error error() const {
    ZX_DEBUG_ASSERT(!ok());
    return status_;
  }
  [[nodiscard]] zx_status_t status() const { return status_.status(); }
  [[nodiscard]] bool ok() const { return status_.ok(); }
  [[nodiscard]] std::string FormatDescription() const;

 private:
  static fidl::EncodedMessage ConversionImpl(
      OutgoingMessage& input, OutgoingMessage::CopiedBytes& buf_bytes,
      std::unique_ptr<zx_handle_t[]>& buf_handles,
      std::unique_ptr<fidl_channel_handle_metadata_t[]>& buf_handle_metadata,
      fidl::Status& out_status);

  fidl::Status status_;
  OutgoingMessage::CopiedBytes buf_bytes_;
  std::unique_ptr<zx_handle_t[]> buf_handles_ = {};
  std::unique_ptr<fidl_channel_handle_metadata_t[]> buf_handle_metadata_ = {};
  fidl::EncodedMessage incoming_message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_MESSAGE_H_
