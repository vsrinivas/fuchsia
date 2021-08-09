// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/llcpp/message_storage.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/nullable.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/variant.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <memory>
#include <vector>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

namespace fidl {

namespace internal {

#ifdef __Fuchsia__
class ClientBase;
class ResponseContext;
#endif  // __Fuchsia__

// This is chosen for performance reasons. It should generally be the same as kIovecChunkSize in
// the kernel.
constexpr uint32_t IovecBufferSize = 16;
using IovecBuffer = zx_channel_iovec_t[IovecBufferSize];

// Marker to allow references/pointers to the unowned input objects in OwnedEncodedMessage.
// This enables iovec optimizations but requires the input objects to stay in scope until the
// encoded result has been consumed.
struct AllowUnownedInputRef {};

}  // namespace internal

// Class representing a FIDL message on the write path.
class OutgoingMessage : public ::fidl::Result {
 public:
  struct ConstructorArgs {
    zx_channel_iovec_t* iovecs;
    uint32_t iovec_capacity;
    zx_handle_disposition_t* handles;
    uint32_t handle_capacity;
    uint8_t* backing_buffer;
    uint32_t backing_buffer_capacity;
  };
  // Creates an object which can manage a FIDL message.
  // |args.iovecs|, |args.handles| and |args.backing_buffer| contain undefined data that will be
  // populated during |Encode|.
  explicit OutgoingMessage(ConstructorArgs args);

  // Copy and move is disabled for the sake of avoiding double handle close.
  // It is possible to implement the move operations with correct semantics if they are
  // ever needed.
  OutgoingMessage(const OutgoingMessage&) = delete;
  OutgoingMessage(OutgoingMessage&&) = delete;
  OutgoingMessage& operator=(const OutgoingMessage&) = delete;
  OutgoingMessage& operator=(OutgoingMessage&&) = delete;
  OutgoingMessage() = delete;
  ~OutgoingMessage();

  // Creates an object which can manage a FIDL message.
  // |c_msg| must contain an already-encoded message.
  static OutgoingMessage FromEncodedCMessage(const fidl_outgoing_msg_t* c_msg);

  // Set the txid in the message header.
  // Requires that there are sufficient bytes to store the header in the buffer.
  void set_txid(zx_txid_t txid) {
    ZX_ASSERT(iovec_actual() >= 1 && iovecs()[0].capacity >= sizeof(fidl_message_header_t));
    // The byte buffer is const becuase the kernel only reads the bytes.
    // const_cast is needed to populate it here.
    reinterpret_cast<fidl_message_header_t*>(const_cast<void*>(iovecs()[0].buffer))->txid = txid;
  }

  zx_channel_iovec_t* iovecs() const { return iovec_message().iovecs; }
  uint32_t iovec_actual() const { return iovec_message().num_iovecs; }
  zx_handle_disposition_t* handles() const { return iovec_message().handles; }
  uint32_t handle_actual() const { return iovec_message().num_handles; }
  fidl_outgoing_msg_t* message() { return &message_; }
  const fidl_outgoing_msg_t* message() const { return &message_; }

  // Returns true iff the bytes in this message are identical to the bytes in the argument.
  bool BytesMatch(const OutgoingMessage& other) const;

  // Holds a heap-allocated continguous copy of the bytes in this message.
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
    is_transactional_ = fidl::IsFidlMessage<FidlType>::value;
    EncodeImpl(FidlType::Type, data);
  }

#ifdef __Fuchsia__
  // Uses |zx_channel_write_etc| to write the message.
  // The message must be in an encoded state before calling |Write|.
  void Write(zx_handle_t channel) { WriteImpl(channel); }

  // Various helper functions for writing to other channel-like types.

  void Write(const ::zx::channel& channel) { Write(channel.get()); }

  void Write(const ::zx::unowned_channel& channel) { Write(channel->get()); }

  template <typename Protocol>
  void Write(::fidl::UnownedClientEnd<Protocol> client_end) {
    Write(client_end.channel());
  }

  template <typename Protocol>
  void Write(const ::fidl::ServerEnd<Protocol>& server_end) {
    Write(server_end.channel().get());
  }

  // For requests with a response, uses zx_channel_call_etc to write the message.
  // Before calling Call, Encode must be called.
  // If the call succeed, |result_bytes| contains the decoded result.
  template <typename FidlType>
  void Call(zx_handle_t channel, uint8_t* result_bytes, uint32_t result_capacity,
            zx_time_t deadline = ZX_TIME_INFINITE) {
    CallImpl(FidlType::Type, channel, result_bytes, result_capacity, deadline);
  }

  // Helper function for making a call over other channel-like types.
  template <typename FidlType, typename Protocol>
  void Call(::fidl::UnownedClientEnd<Protocol> client_end, uint8_t* result_bytes,
            uint32_t result_capacity, zx_time_t deadline = ZX_TIME_INFINITE) {
    CallImpl(FidlType::Type, client_end.handle(), result_bytes, result_capacity, deadline);
  }

  // For asynchronous clients, writes a request.
  ::fidl::Result Write(::fidl::internal::ClientBase* client,
                       ::fidl::internal::ResponseContext* context);
#endif

  bool is_transactional() const { return is_transactional_; }

 protected:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Result(::fidl::Result::Ok()), message_(msg), handle_capacity_(handle_capacity) {}

  void EncodeImpl(const fidl_type_t* message_type, void* data);

#ifdef __Fuchsia__
  void WriteImpl(zx_handle_t channel);
  void CallImpl(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
                uint32_t result_capacity, zx_time_t deadline);
#endif

  uint32_t iovec_capacity() const { return iovec_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  uint32_t backing_buffer_capacity() const { return backing_buffer_capacity_; }
  uint8_t* backing_buffer() const { return backing_buffer_; }

 private:
  explicit OutgoingMessage(const fidl_outgoing_msg_t* msg);

  fidl_outgoing_msg_iovec_t& iovec_message() {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }
  const fidl_outgoing_msg_iovec_t& iovec_message() const {
    ZX_DEBUG_ASSERT(message_.type == FIDL_OUTGOING_MSG_TYPE_IOVEC);
    return message_.iovec;
  }

  using Result::SetResult;

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

enum class WireFormatVersion {
  kV1,
  kV2,
};

constexpr WireFormatVersion kLLCPPInMemoryWireFormatVersion = WireFormatVersion::kV1;

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
// as |fidl::DecodedMessage<FidlType>|.
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
  // Creates an object which can manage a FIDL message. Allocated memory is not owned by
  // the |IncomingMessage|, but handles are owned by it and cleaned up when the
  // |IncomingMessage| is destructed.
  //
  // The bytes must represent a transactional message. See
  // https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages
  IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles,
                  uint32_t handle_actual);

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
  IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles,
                  uint32_t handle_actual, SkipMessageHeaderValidationTag);

  // Creates an empty incoming message representing an error (e.g. failed to read from
  // a channel).
  //
  // |failure| must contain an error result.
  explicit IncomingMessage(const ::fidl::Result& failure);

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

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  uint32_t byte_actual() const { return message_.num_bytes; }

  zx_handle_info_t* handles() const { return message_.handles; }
  uint32_t handle_actual() const { return message_.num_handles; }

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
  // Only |fidl::DecodedMessage<T>| instances may decode this message.
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
    Decode(FidlType::Type, out_transformed_buffer);
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
    Decode(wire_format_version, FidlType::Type, out_transformed_buffer);
  }

  // Release the handle ownership after the message has been converted to its
  // decoded form. When used standalone and not as part of a |Decode|, this
  // method is only useful when interfacing with C APIs.
  void ReleaseHandles() { message_.num_handles = 0; }

  void MoveImpl(IncomingMessage&& other) noexcept {
    message_ = other.message_;
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
              std::unique_ptr<uint8_t[]>* out_transformed_buffer);

  // Performs basic transactional message header validation and sets the |fidl::Result| fields
  // accordingly.
  void Validate();

  fidl_incoming_msg_t message_;
  bool is_transactional_ = false;
};

#ifdef __Fuchsia__

// Wrapper around |zx_channel_read_etc| that instantiates an |IncomingMessage|
// with the contents read from the |channel|, referencing the |bytes_storage|
// and |handles_storage| buffers. The channel should contain transactional FIDL
// messages, which the instantiated |IncomingMessage| will automatically validate.
//
// Error information is embedded in the returned |IncomingMessage| when applicable.
IncomingMessage ChannelReadEtc(zx_handle_t channel, uint32_t options,
                               ::fidl::BufferSpan bytes_storage,
                               cpp20::span<zx_handle_info_t> handles_storage);

#endif  // __Fuchsia__

namespace internal {

// DecodedMessageBase implements the common behavior to all
// |fidl::DecodedMessage<T>| subclasses. They may be created from an incoming
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
    static_assert(fidl::IsFidlMessage<FidlType>::value);
    msg.Decode<FidlType>(&allocated_buffer_);
    bytes_ = msg.bytes();
    SetResult(msg);
  }

  // Creates an |DecodedMessageBase| by decoding the incoming message |msg| as the specified
  // |wire_format_version|.
  // Consumes |msg|.
  explicit DecodedMessageBase(internal::WireFormatVersion wire_format_version,
                              ::fidl::IncomingMessage&& msg) {
    static_assert(!fidl::IsFidlMessage<FidlType>::value);
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

// This class owns a message of |FidlType| and encodes the message automatically upon construction
// into a byte buffer.
template <typename FidlType>
using OwnedEncodedMessage = typename FidlType::OwnedEncodedMessage;

// This class manages the handles within |FidlType| and encodes the message automatically upon
// construction. Different from |OwnedEncodedMessage|, it takes in a caller-allocated buffer and
// uses that as the backing storage for the message. The buffer must outlive instances of this
// class.
template <typename FidlType>
using UnownedEncodedMessage = typename FidlType::UnownedEncodedMessage;

// This class manages the handles within |FidlType| and decodes the message automatically upon
// construction. It always borrows external buffers for the backing storage of the message.
// This class should mostly be used for tests.
template <typename FidlType>
using DecodedMessage = typename FidlType::DecodedMessage;

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
  static fidl::IncomingMessage ConversionImpl(OutgoingMessage& input,
                                              OutgoingMessage::CopiedBytes& buf_bytes,
                                              std::unique_ptr<zx_handle_info_t[]>& buf_handles);

  OutgoingMessage::CopiedBytes buf_bytes_;
  std::unique_ptr<zx_handle_info_t[]> buf_handles_ = {};
  fidl::IncomingMessage incoming_message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
