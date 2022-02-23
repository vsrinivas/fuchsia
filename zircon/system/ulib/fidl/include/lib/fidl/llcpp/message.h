// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/nullable.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/variant.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

namespace fidl_testing {
// Forward declaration of test helpers to support friend declaration.
class MessageChecker;
}  // namespace fidl_testing

namespace fidl {

namespace internal {

constexpr WireFormatVersion kLLCPPWireFormatVersion = WireFormatVersion::kV2;

// This is chosen for performance reasons. It should generally be the same as kIovecChunkSize in
// the kernel.
constexpr uint32_t IovecBufferSize = 16;
using IovecBuffer = zx_channel_iovec_t[IovecBufferSize];

// Marker to allow references/pointers to the unowned input objects in OwnedEncodedMessage.
// This enables iovec optimizations but requires the input objects to stay in scope until the
// encoded result has been consumed.
struct AllowUnownedInputRef {};

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
class OutgoingMessage : public ::fidl::Result {
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

  struct ConstructorArgs {
    const internal::TransportVTable* transport_vtable;
    zx_channel_iovec_t* iovecs;
    uint32_t iovec_capacity;
    zx_handle_t* handles;
    fidl_handle_metadata_t* handle_metadata;
    uint32_t handle_capacity;
    uint8_t* backing_buffer;
    uint32_t backing_buffer_capacity;
  };
  // Creates an object which can manage a FIDL message.
  // |args.iovecs|, |args.handles| and |args.backing_buffer| contain undefined data that will be
  // populated during |Encode|.
  static OutgoingMessage CreateInternal(ConstructorArgs args) { return OutgoingMessage(args); }

  // Creates an empty outgoing message representing an error.
  //
  // |failure| must contain an error result.
  explicit OutgoingMessage(const ::fidl::Result& failure);

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
    is_transactional_ = fidl::IsFidlTransactionalMessage<FidlType>::value;
    EncodeImpl(fidl::internal::WireFormatVersion::kV1, fidl::TypeTraits<FidlType>::kType, data);
  }

  template <typename FidlType>
  void Encode(fidl::internal::WireFormatVersion wire_format_version, FidlType* data) {
    is_transactional_ = fidl::IsFidlTransactionalMessage<FidlType>::value;
    EncodeImpl(wire_format_version, fidl::TypeTraits<FidlType>::kType, data);
  }

  void Validate__InternalMayBreak(fidl::internal::WireFormatVersion wire_format_version,
                                  const fidl_type_t* type);

  // Various helper functions for writing to other channel-like types.

  void Write(internal::AnyUnownedTransport transport, WriteOptions options = {});

  template <typename TransportObject>
  void Write(TransportObject&& transport, WriteOptions options = {}) {
    Write(internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport)),
          std::move(options));
  }

  template <typename FidlType, typename TransportObject,
            typename = std::enable_if_t<
                !internal::AssociatedTransport<TransportObject>::TransportProvidesReadBuffer>>
  void Call(TransportObject&& transport, uint8_t* result_bytes, uint32_t result_byte_capacity,
            CallOptions options = {}) {
    fidl_handle_t result_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    typename internal::AssociatedTransport<TransportObject>::HandleMetadata
        result_handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
    CallImplForCallerProvidedBuffer(
        internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport)),
        fidl::TypeTraits<FidlType>::kType, result_bytes, result_byte_capacity, result_handles,
        reinterpret_cast<fidl_handle_metadata_t*>(result_handle_metadata),
        ZX_CHANNEL_MAX_MSG_HANDLES, std::move(options));
  }

  template <typename FidlType, typename TransportObject,
            typename = std::enable_if_t<
                internal::AssociatedTransport<TransportObject>::TransportProvidesReadBuffer>>
  void Call(TransportObject&& transport, uint8_t** out_bytes, uint32_t* out_num_bytes,
            CallOptions options = {}) {
    CallImplForTransportProvidedBuffer(
        internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport)),
        fidl::TypeTraits<FidlType>::kType, out_bytes, out_num_bytes, std::move(options));
  }

  bool is_transactional() const { return is_transactional_; }

 protected:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Result(::fidl::Result::Ok()), message_(msg), handle_capacity_(handle_capacity) {}

  void EncodeImpl(fidl::internal::WireFormatVersion wire_format_version,
                  const fidl_type_t* message_type, void* data);

  uint32_t iovec_capacity() const { return iovec_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  uint32_t backing_buffer_capacity() const { return backing_buffer_capacity_; }
  uint8_t* backing_buffer() const { return backing_buffer_; }

 private:
  friend ::fidl_testing::MessageChecker;

  explicit OutgoingMessage(ConstructorArgs args);
  explicit OutgoingMessage(const fidl_outgoing_msg_t* msg, bool is_transactional);

  void DecodeImplForCall(const internal::CodingConfig& coding_config,
                         const fidl_type_t* response_type, uint8_t* bytes,
                         uint32_t* in_out_num_bytes, fidl_handle_t* handles,
                         fidl_handle_metadata_t* handle_metadata, uint32_t num_handles);

  void CallImplForCallerProvidedBuffer(internal::AnyUnownedTransport transport,
                                       const fidl_type_t* response_type, uint8_t* result_bytes,
                                       uint32_t result_byte_capacity, fidl_handle_t* result_handles,
                                       fidl_handle_metadata_t* result_handle_metadata,
                                       uint32_t result_handle_capacity, CallOptions options);

  void CallImplForTransportProvidedBuffer(internal::AnyUnownedTransport transport,
                                          const fidl_type_t* response_type, uint8_t** out_bytes,
                                          uint32_t* out_num_bytes, CallOptions options);

  fidl_outgoing_msg_iovec_t& iovec_message() {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }
  const fidl_outgoing_msg_iovec_t& iovec_message() const {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }

  using Result::SetResult;

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
};

namespace internal {

template <typename T>
class DecodedMessageBase;

}  // namespace internal

// |IncomingMessage| represents a FIDL message on the read path.
// Each instantiation of the class should only be used for one message.
//
// |IncomingMessage|s are created with the results from reading from a channel.
// By default, it assumes it is a transactional message, and automatically
// performs necessary validation on the message header - users may opt out
// via the |kSkipMessageHeaderValidation| constructor overload in the case of
// regular FIDL type encoding/decoding.
//
// |IncomingMessage| relinquishes the ownership of the handles after decoding.
// Instead, callers must adopt the decoded content into another RAII class, such
// as |fidl::unstable::DecodedMessage<FidlType>|.
//
// Functions that take |IncomingMessage&| conditionally take ownership of the
// message. For functions in the public API, they must then indicate through
// their return value if they took ownership. For functions in the binding
// internals, it is sufficient to only document the conditions where minimum
// overhead is desired.
//
// Functions that take |IncomingMessage&&| always take ownership of the message.
// In practice, this means that they must either decode the message, or close
// the handles, or move the message into a deeper function that takes
// |IncomingMessage&&|.
//
// For efficiency, errors are stored inside this object. Callers must check for
// errors after construction, and after performing each operation on the object.
//
// An |IncomingMessage| may be created from |fidl::ChannelReadEtc|:
//
//     fidl::IncomingMessage msg = fidl::ChannelReadEtc(handle, 0, byte_span, handle_span);
//     if (!msg.ok()) { /* ... error handling ... */ }
//
class IncomingMessage : public ::fidl::Result {
 public:
  // Creates an object which can manage a FIDL channel message. Allocated memory is
  // not owned by the |IncomingMessage|, but handles are owned by it and cleaned up when the
  // |IncomingMessage| is destructed.
  //
  // The bytes must represent a transactional message. See
  // https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
  template <typename HandleMetadata>
  static IncomingMessage Create(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                HandleMetadata* handle_metadata, uint32_t handle_actual) {
    return Create<typename internal::AssociatedTransport<HandleMetadata>>(
        bytes, byte_actual, handles, handle_metadata, handle_actual);
  }

  // Creates an object which can manage a FIDL message. Allocated memory is not owned by
  // the |IncomingMessage|, but handles are owned by it and cleaned up when the
  // |IncomingMessage| is destructed.
  //
  // The bytes must represent a transactional message. See
  // https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
  template <typename Transport>
  static IncomingMessage Create(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                typename Transport::HandleMetadata* handle_metadata,
                                uint32_t handle_actual) {
    return IncomingMessage(&Transport::VTable, bytes, byte_actual, handles,
                           reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
                           handle_actual);
  }

  // Creates an |IncomingMessage| from a C |fidl_incoming_msg_t| already in
  // encoded form. This should only be used when interfacing with C APIs.
  // The handles in |c_msg| are owned by the returned |IncomingMessage| object.
  //
  // The bytes must represent a transactional message.
  static IncomingMessage FromEncodedCMessage(const fidl_incoming_msg_t* c_msg);

  struct SkipMessageHeaderValidationTag {};

  // A marker that instructs the constructor of |IncomingMessage| to skip
  // validating the message header. This is useful when the message is not a
  // transactional message.
  constexpr inline static auto kSkipMessageHeaderValidation = SkipMessageHeaderValidationTag{};

  // An overload for when the bytes do not represent a transactional message.
  //
  // This constructor should be rarely used in practice. When decoding
  // FIDL types that are not transactional messages (e.g. tables), consider
  // using the constructor in |FidlType::DecodedMessage|, which delegates
  // here appropriately.
  template <typename HandleMetadata>
  static IncomingMessage Create(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                HandleMetadata* handle_metadata, uint32_t handle_actual,
                                SkipMessageHeaderValidationTag) {
    return Create<internal::AssociatedTransport<HandleMetadata>>(
        bytes, byte_actual, handles, handle_metadata, handle_actual, kSkipMessageHeaderValidation);
  }

  // An overload for when the bytes do not represent a transactional message.
  //
  // This constructor should be rarely used in practice. When decoding
  // FIDL types that are not transactional messages (e.g. tables), consider
  // using the constructor in |FidlType::DecodedMessage|, which delegates
  // here appropriately.
  template <typename Transport>
  static IncomingMessage Create(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles,
                                typename Transport::HandleMetadata* handle_metadata,
                                uint32_t handle_actual, SkipMessageHeaderValidationTag) {
    return IncomingMessage(&Transport::VTable, bytes, byte_actual, handles,
                           reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
                           handle_actual, kSkipMessageHeaderValidation);
  }

  // Creates an empty incoming message representing an error (e.g. failed to read from
  // a channel).
  //
  // |failure| must contain an error result.
  static IncomingMessage Create(const ::fidl::Result& failure) { return IncomingMessage(failure); }

  IncomingMessage(const IncomingMessage&) = delete;
  IncomingMessage& operator=(const IncomingMessage&) = delete;

  IncomingMessage(IncomingMessage&& other) noexcept : ::fidl::Result(other) {
    MoveImpl(std::move(other));
  }
  IncomingMessage& operator=(IncomingMessage&& other) noexcept {
    ::fidl::Result::operator=(other);
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  ~IncomingMessage();

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

  bool is_transactional() const { return is_transactional_; }

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  uint32_t byte_actual() const { return message_.num_bytes; }

  zx_handle_t* handles() const { return message_.handles; }
  uint32_t handle_actual() const { return message_.num_handles; }

  template <typename Transport>
  typename Transport::HandleMetadata* handle_metadata() const {
    ZX_ASSERT(Transport::VTable.type == transport_vtable_->type);
    return reinterpret_cast<typename Transport::HandleMetadata*>(message_.handle_metadata);
  }

  // Convert the incoming message to its C API counterpart, releasing the
  // ownership of handles to the caller in the process. This consumes the
  // |IncomingMessage|.
  //
  // This should only be called while the message is in its encoded form.
  fidl_incoming_msg_t ReleaseToEncodedCMessage() &&;

  // Closes the handles managed by this message. This may be used when the
  // code would like to consume a |IncomingMessage&&| and close its handles,
  // but does not want to incur the overhead of moving it into a regular
  // |IncomingMessage| object, and running the destructor.
  //
  // This consumes the |IncomingMessage|.
  void CloseHandles() &&;

 private:
  explicit IncomingMessage(const ::fidl::Result& failure);
  IncomingMessage(const internal::TransportVTable* transport_vtable, uint8_t* bytes,
                  uint32_t byte_actual, zx_handle_t* handles,
                  fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual);
  IncomingMessage(const internal::TransportVTable* transport_vtable, uint8_t* bytes,
                  uint32_t byte_actual, zx_handle_t* handles,
                  fidl_handle_metadata_t* handle_metadata, uint32_t handle_actual,
                  SkipMessageHeaderValidationTag);

  // Only |fidl::unstable::DecodedMessage<T>| instances may decode this message.
  template <typename T>
  friend class internal::DecodedMessageBase;

  // Decodes the message using |FidlType|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  //
  // The first 16 bytes of the message must be the FIDL message header and are used for
  // determining the wire format version for decoding.
  //
  // On success, the handles owned by |IncomingMessage| are transferred to the decoded bytes.
  // If a buffer needs to be allocated during decode, |out_transformed_buffer| will contain that
  // buffer. This buffer will be stored on DecodedMessageBase and stays in scope for the lifetime
  // of the decoded message, which is responsible for freeing it.
  //
  // This method should be used after a read.
  template <typename FidlType>
  void Decode(std::unique_ptr<uint8_t[]>* out_transformed_buffer) {
    ZX_ASSERT(is_transactional_);

    // If this is an empty message, there is nothing to decode, so exit early.
    if (fidl::TypeTraits<FidlType>::kType == nullptr) {
      return;
    }
    Decode(fidl::TypeTraits<FidlType>::kType, out_transformed_buffer);
  }

  // Decodes the message using |FidlType| for the specified |wire_format_version|. If this
  // operation succeed, |status()| is ok and |bytes()| contains the decoded object.
  //
  // On success, the handles owned by |IncomingMessage| are transferred to the decoded bytes.
  //
  // This method should be used after a read.
  template <typename FidlType>
  void Decode(internal::WireFormatVersion wire_format_version,
              std::unique_ptr<uint8_t[]>* out_transformed_buffer) {
    ZX_ASSERT(!is_transactional_);
    ZX_ASSERT(fidl::TypeTraits<FidlType>::kType != nullptr);
    Decode(wire_format_version, fidl::TypeTraits<FidlType>::kType,
           fidl::IsFidlTransactionalMessage<FidlType>::value, out_transformed_buffer);
  }

  // Release the handle ownership after the message has been converted to its
  // decoded form. When used standalone and not as part of a |Decode|, this
  // method is only useful when interfacing with C APIs.
  void ReleaseHandles() { message_.num_handles = 0; }

  void MoveImpl(IncomingMessage&& other) noexcept {
    transport_vtable_ = other.transport_vtable_;
    message_ = other.message_;
    is_transactional_ = other.is_transactional_;
    other.ReleaseHandles();
  }

  // Decodes the message using |message_type|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  //
  // The first 16 bytes of the message must be the FIDL message header and are used for
  // determining the wire format version for decoding.
  //
  // On success, the handles owned by |IncomingMessage| are transferred to the decoded bytes.
  // If a buffer needs to be allocated during decode, |out_transformed_buffer| will contain that
  // buffer. This buffer will be stored on DecodedMessageBase and stays in scope for the lifetime
  // of the decoded message, which is responsible for freeing it.
  //
  // This method should be used after a read.
  void Decode(const fidl_type_t* message_type, std::unique_ptr<uint8_t[]>* out_transformed_buffer);

  // Decodes the message using |message_type| for the specified |wire_format_version|. If this
  // operation succeed, |status()| is ok and |bytes()| contains the decoded object.
  //
  // On success, the handles owned by |IncomingMessage| are transferred to the decoded bytes.
  // If a buffer needs to be allocated during decode, |out_transformed_buffer| will contain that
  // buffer. This buffer will be stored on DecodedMessageBase and stays in scope for the lifetime
  // of the decoded message, which is responsible for freeing it.
  //
  // This method should be used after a read.
  void Decode(internal::WireFormatVersion wire_format_version, const fidl_type_t* message_type,
              bool is_transactional, std::unique_ptr<uint8_t[]>* out_transformed_buffer);

  // Performs basic transactional message header validation and sets the |fidl::Result| fields
  // accordingly.
  void ValidateHeader();

  const internal::TransportVTable* transport_vtable_ = nullptr;
  fidl_incoming_msg_t message_;
  bool is_transactional_ = false;
};

// Reads a message from |transport| using the |bytes_storage| and |handles_storage|
// buffers as needed.
//
// Error information is embedded in the returned |IncomingMessage| when applicable.
template <typename TransportObject>
IncomingMessage MessageRead(TransportObject&& transport, ::fidl::BufferSpan bytes_storage,
                            fidl_handle_t* handle_storage,
                            typename internal::AssociatedTransport<TransportObject>::HandleMetadata*
                                handle_metadata_storage,
                            uint32_t handle_capacity, const ReadOptions& options = {}) {
  auto type_erased_transport =
      internal::MakeAnyUnownedTransport(std::forward<TransportObject>(transport));
  uint32_t num_bytes, num_handles;
  zx_status_t status = type_erased_transport.read(
      options, bytes_storage.data, bytes_storage.capacity, handle_storage, handle_metadata_storage,
      handle_capacity, &num_bytes, &num_handles);
  if (status != ZX_OK) {
    return IncomingMessage::Create(fidl::Result::TransportError(status));
  }
  return IncomingMessage::Create(bytes_storage.data, num_bytes, handle_storage,
                                 handle_metadata_storage, num_handles);
}

namespace internal {

// DecodedMessageBase implements the common behavior to all
// |fidl::unstable::DecodedMessage<T>| subclasses. They may be created from an incoming
// message in encoded form, in which case they would perform the necessary
// decoding and own the decoded handles via RAII.
//
// |DecodedMessageBase| should never be instantiated directly. Rather, a
// subclass should be defined which adds the FIDL type-specific handle RAII
// behavior.
template <typename FidlType>
class DecodedMessageBase : public ::fidl::Result {
 public:
  // Creates an |DecodedMessageBase| by decoding the incoming message |msg|.
  // Consumes |msg|.
  //
  // The first 16 bytes of the message are assumed to be the FIDL message header and are used
  // for determining the wire format version for decoding.
  explicit DecodedMessageBase(::fidl::IncomingMessage&& msg) {
    static_assert(fidl::IsFidlTransactionalMessage<FidlType>::value);
    msg.Decode<FidlType>(&allocated_buffer_);
    bytes_ = msg.bytes();
    SetResult(msg);
  }

  // Creates an |DecodedMessageBase| by decoding the incoming message |msg| as the specified
  // |wire_format_version|.
  // Consumes |msg|.
  explicit DecodedMessageBase(internal::WireFormatVersion wire_format_version,
                              ::fidl::IncomingMessage&& msg) {
    static_assert(!fidl::IsFidlTransactionalMessage<FidlType>::value);
    msg.Decode<FidlType>(wire_format_version, &allocated_buffer_);
    bytes_ = msg.bytes();
    SetResult(msg);
  }

  // Creates an empty decoded message representing an error (e.g. failed to read
  // from a channel).
  //
  // |failure| must contain an error result.
  explicit DecodedMessageBase(const ::fidl::Result& failure) {
    ZX_DEBUG_ASSERT(!failure.ok());
    SetResult(failure);
  }

 protected:
  DecodedMessageBase(const DecodedMessageBase&) = delete;
  DecodedMessageBase(DecodedMessageBase&&) = delete;
  DecodedMessageBase& operator=(const DecodedMessageBase&) = delete;
  DecodedMessageBase& operator=(DecodedMessageBase&&) = delete;

  ~DecodedMessageBase() = default;

  uint8_t* bytes() const { return bytes_; }

  void ResetBytes() { bytes_ = nullptr; }

 private:
  uint8_t* bytes_ = nullptr;
  std::unique_ptr<uint8_t[]> allocated_buffer_;
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
class UnownedEncodedMessage final {
 public:
  UnownedEncodedMessage(uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(::fidl::internal::IovecBufferSize, backing_buffer,
                              backing_buffer_size, response) {}
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint8_t* backing_buffer, uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(wire_format_version, ::fidl::internal::IovecBufferSize,
                              backing_buffer, backing_buffer_size, response) {}
  UnownedEncodedMessage(uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* response)
      : UnownedEncodedMessage(fidl::internal::kLLCPPWireFormatVersion, iovec_capacity,
                              backing_buffer, backing_buffer_size, response) {}
  UnownedEncodedMessage(fidl::internal::WireFormatVersion wire_format_version,
                        uint32_t iovec_capacity, uint8_t* backing_buffer,
                        uint32_t backing_buffer_size, FidlType* response)
      : message_(::fidl::OutgoingMessage::CreateInternal(::fidl::OutgoingMessage::ConstructorArgs{
            .transport_vtable = &Transport::VTable,
            .iovecs = iovecs_,
            .iovec_capacity = iovec_capacity,
            .handles = handle_storage_.data(),
            .handle_metadata =
                reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata_storage_.data()),
            .handle_capacity = kNumHandles,
            .backing_buffer = backing_buffer,
            .backing_buffer_capacity = backing_buffer_size,
        })),
        wire_format_version_(wire_format_version) {
    ZX_ASSERT(iovec_capacity <= std::size(iovecs_));
    message_.Encode<FidlType>(wire_format_version, response);
  }
  UnownedEncodedMessage(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage(UnownedEncodedMessage&&) = delete;
  UnownedEncodedMessage* operator=(const UnownedEncodedMessage&) = delete;
  UnownedEncodedMessage* operator=(UnownedEncodedMessage&&) = delete;

  zx_status_t status() const { return message_.status(); }
#ifdef __Fuchsia__
  const char* status_string() const { return message_.status_string(); }
#endif
  bool ok() const { return message_.status() == ZX_OK; }
  std::string FormatDescription() const { return message_.FormatDescription(); }
  const char* lossy_description() const { return message_.lossy_description(); }
  const ::fidl::Result& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_; }

  template <typename TransportObject>
  void Write(TransportObject&& client, WriteOptions options = {}) {
    message_.Write(std::forward<TransportObject>(client), std::move(options));
  }

 private:
  static constexpr uint32_t kNumHandles =
      fidl::internal::ClampedHandleCount<FidlType, fidl::MessageDirection::kSending>();
  std::array<zx_handle_t, kNumHandles> handle_storage_;
  std::array<typename Transport::HandleMetadata, kNumHandles> handle_metadata_storage_;
  fidl::internal::IovecBuffer iovecs_;
  fidl::OutgoingMessage message_;
  fidl::internal::WireFormatVersion wire_format_version_;
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
      : message_(::fidl::internal::IovecBufferSize, backing_buffer_.data(),
                 static_cast<uint32_t>(backing_buffer_.size()), response) {}
  explicit OwnedEncodedMessage(::fidl::internal::AllowUnownedInputRef allow_unowned,
                               fidl::internal::WireFormatVersion wire_format_version,
                               FidlType* response)
      : message_(wire_format_version, ::fidl::internal::IovecBufferSize, backing_buffer_.data(),
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
  const ::fidl::Result& error() const { return message_.error(); }

  ::fidl::OutgoingMessage& GetOutgoingMessage() { return message_.GetOutgoingMessage(); }

  template <typename TransportObject>
  void Write(TransportObject&& client, WriteOptions options = {}) {
    message_.Write(std::forward<TransportObject>(client), std::move(options));
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

// Specialization for transactional messages.
template <typename FidlType, typename Transport>
class DecodedMessage<FidlType, Transport,
                     std::void_t<decltype(fidl::TypeTraits<FidlType>::kMessageKind)>>
    final : public ::fidl::internal::DecodedMessageBase<FidlType> {
  using Base = ::fidl::internal::DecodedMessageBase<FidlType>;

 public:
  using Base::DecodedMessageBase;

  DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles = nullptr,
                 typename Transport::HandleMetadata* handle_metadata = nullptr,
                 uint32_t handle_actual = 0)
      : Base(::fidl::IncomingMessage::Create(bytes, byte_actual, handles, handle_metadata,
                                             handle_actual)) {}

  ~DecodedMessage() {
    if constexpr (::fidl::IsResource<FidlType>::value) {
      if (Base::ok() && (PrimaryObject() != nullptr)) {
        PrimaryObject()->_CloseHandles();
      }
    }
  }

  FidlType* PrimaryObject() {
    ZX_DEBUG_ASSERT(Base::ok());
    return reinterpret_cast<FidlType*>(Base::bytes());
  }

  // Release the ownership of the decoded message. That means that the handles won't be closed
  // When the object is destroyed.
  // After calling this method, the |DecodedMessage| object should not be used anymore.
  void ReleasePrimaryObject() { ::fidl::internal::DecodedMessageBase<FidlType>::ResetBytes(); }
};

// Specialization for non-transactional types (tables, structs, unions).
template <typename FidlType, typename Transport>
class DecodedMessage<FidlType, Transport,
                     std::enable_if_t<::fidl::IsFidlObject<FidlType>::value, void>>
    final : public ::fidl::internal::DecodedMessageBase<FidlType> {
  using Base = ::fidl::internal::DecodedMessageBase<FidlType>;

 public:
  using Base::DecodedMessageBase;

  DecodedMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_t* handles = nullptr,
                 typename Transport::HandleMetadata* handle_metadata = nullptr,
                 uint32_t handle_actual = 0)
      : Base(::fidl::internal::WireFormatVersion::kV1,
             ::fidl::IncomingMessage::Create(
                 bytes, byte_actual, handles, handle_metadata, handle_actual,
                 ::fidl::IncomingMessage::kSkipMessageHeaderValidation)) {}

  // Internal constructor for specifying a specific wire format version.
  DecodedMessage(::fidl::internal::WireFormatVersion wire_format_version, uint8_t* bytes,
                 uint32_t byte_actual, zx_handle_t* handles = nullptr,
                 typename Transport::HandleMetadata* handle_metadata = nullptr,
                 uint32_t handle_actual = 0)
      : Base(wire_format_version, ::fidl::IncomingMessage::Create(
                                      bytes, byte_actual, handles, handle_metadata, handle_actual,
                                      ::fidl::IncomingMessage::kSkipMessageHeaderValidation)) {}

  DecodedMessage(const fidl_incoming_msg_t* c_msg)
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

  ~DecodedMessage() {
    if constexpr (::fidl::IsResource<FidlType>::value) {
      if (Base::ok() && (PrimaryObject() != nullptr)) {
        PrimaryObject()->_CloseHandles();
      }
    }
  }

  FidlType* PrimaryObject() {
    ZX_DEBUG_ASSERT(Base::ok());
    return reinterpret_cast<FidlType*>(Base::bytes());
  }

  // Release the ownership of the decoded message. That means that the handles won't be closed
  // When the object is destroyed.
  // After calling this method, the |DecodedMessage| object should not be used anymore.
  void ReleasePrimaryObject() { Base::ResetBytes(); }
};

}  // namespace unstable

// Holds the result of converting an outgoing message to an incoming message.
//
// |OutgoingToIncomingMessage| objects own the bytes and handles resulting from
// conversion.
class OutgoingToIncomingMessage {
 public:
  // Converts an outgoing message to an incoming message.
  //
  // In doing so, it will make syscalls to fetch rights and type information
  // of any provided handles. The caller is responsible for ensuring that
  // returned handle rights and object types are checked appropriately.
  //
  // The constructed |OutgoingToIncomingMessage| will take ownership over
  // handles from the input |OutgoingMessage|.
  explicit OutgoingToIncomingMessage(OutgoingMessage& input);

  ~OutgoingToIncomingMessage() = default;

  fidl::IncomingMessage& incoming_message() & {
    ZX_DEBUG_ASSERT(ok());
    return incoming_message_;
  }

  [[nodiscard]] zx_status_t status() const { return incoming_message_.status(); }
  [[nodiscard]] bool ok() const { return incoming_message_.ok(); }
  [[nodiscard]] std::string FormatDescription() const;

 private:
  static fidl::IncomingMessage ConversionImpl(
      OutgoingMessage& input, OutgoingMessage::CopiedBytes& buf_bytes,
      std::unique_ptr<zx_handle_t[]>& buf_handles,
      std::unique_ptr<fidl_channel_handle_metadata_t[]>& buf_handle_metadata);

  OutgoingMessage::CopiedBytes buf_bytes_;
  std::unique_ptr<zx_handle_t[]> buf_handles_ = {};
  std::unique_ptr<fidl_channel_handle_metadata_t[]> buf_handle_metadata_ = {};
  fidl::IncomingMessage incoming_message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
