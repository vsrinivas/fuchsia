// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/txn_header.h>
#include <lib/stdcompat/variant.h>
#include <zircon/assert.h>

#include <memory>
#include <vector>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/server_end.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#endif

namespace fidl {

namespace internal {

#ifdef __Fuchsia__
class ClientBase;
class ResponseContext;
#endif

// Create a type for the 1-element iovec buffer used as the output of fidl_encode_iovec.
// TODO(fxbug.dev/66977) Replace this with a resizable buffer type.
constexpr size_t IovecBufferSize = 1;
using IovecBuffer = zx_channel_iovec_t[IovecBufferSize];

class OutgoingMessageResultSetter;

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
    explicit CopiedBytes(OutgoingMessage& msg);

    std::vector<uint8_t> bytes_;

    friend class OutgoingMessage;
  };

  // Create a heap-allocated contiguous copy of the bytes in this message.
  CopiedBytes CopyBytes() { return CopiedBytes(*this); }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { iovec_message().num_handles = 0; }

  // Encodes the data.
  template <typename FidlType>
  void Encode(FidlType* data) {
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

 protected:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Result(ZX_OK, nullptr), message_(msg), handle_capacity_(handle_capacity) {}

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

  void SetResult(zx_status_t status, const char* error) { Result::SetResult(status, error); }

  fidl_outgoing_msg_t message_ = {};
  uint32_t iovec_capacity_ = 0;
  uint32_t handle_capacity_ = 0;
  uint32_t backing_buffer_capacity_ = 0;
  uint8_t* backing_buffer_ = nullptr;

  // If OutgoingMessage is constructed with a fidl_outgoing_msg_t* that contains bytes
  // rather than iovec, it is converted to a single-element iovec pointing to the bytes.
  zx_channel_iovec_t converted_byte_message_iovec_ = {};

  friend class internal::OutgoingMessageResultSetter;
};

namespace internal {

class OutgoingMessageResultSetter {
 public:
  static void SetResult(OutgoingMessage& message, zx_status_t status, const char* error) {
    message.SetResult(status, error);
  }
};

// Class representing a FIDL message on the read path.
// Each instantiation of the class should only be used for one message.
class IncomingMessage : public ::fidl::Result {
 public:
  // Creates an object which can manage a FIDL message. Allocated memory is not owned by
  // the |IncomingMessage|, but handles are owned by it and cleaned up when the
  // |IncomingMessage| is destructed.
  // If Decode has been called, the handles have been transferred to the allocated memory.
  IncomingMessage();
  IncomingMessage(uint8_t* bytes, uint32_t byte_actual, zx_handle_info_t* handles,
                  uint32_t handle_actual);
  explicit IncomingMessage(const fidl_incoming_msg_t* msg)
      : ::fidl::Result(ZX_OK, nullptr), message_(*msg) {}
  // Copy and move is disabled for the sake of avoiding double handle close.
  // It is possible to implement the move operations with correct semantics if they are
  // ever needed.
  IncomingMessage(const IncomingMessage&) = delete;
  IncomingMessage(IncomingMessage&&) = delete;
  IncomingMessage& operator=(const IncomingMessage&) = delete;
  IncomingMessage& operator=(IncomingMessage&&) = delete;
  ~IncomingMessage();

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  zx_handle_info_t* handles() const { return message_.handles; }
  uint32_t byte_actual() const { return message_.num_bytes; }
  uint32_t handle_actual() const { return message_.num_handles; }
  fidl_incoming_msg_t* message() { return &message_; }

 protected:
  // Reset the byte pointer. Used to relase the control onwership of the bytes.
  void ResetBytes() { message_.bytes = nullptr; }

  // Decodes the message using |FidlType|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  // This method should be used after a read.
  template <typename FidlType>
  void Decode() {
    Decode(FidlType::Type);
  }

 private:
  // Decodes the message using |message_type|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  // This method should be used after a read.
  void Decode(const fidl_type_t* message_type);

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { message_.num_handles = 0; }

  fidl_incoming_msg_t message_;
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

// Holds the result of a call to |OutgoingToIncomingMessage|.
//
// |OutgoingToIncomingMessageResult| objects own the bytes and handles resulting from
// conversion.
class OutgoingToIncomingMessageResult {
 public:
  OutgoingToIncomingMessageResult(OutgoingToIncomingMessageResult&& to_move) noexcept;
  explicit OutgoingToIncomingMessageResult(fidl_incoming_msg_t incoming_message, zx_status_t status,
                                           OutgoingMessage::CopiedBytes buf_bytes,
                                           std::unique_ptr<zx_handle_info_t[]> buf_handles)
      : incoming_message_(incoming_message),
        status_(status),
        buf_bytes_(std::move(buf_bytes)),
        buf_handles_(std::move(buf_handles)) {}
  ~OutgoingToIncomingMessageResult();
  fidl_incoming_msg_t* incoming_message() {
    ZX_DEBUG_ASSERT(ok());
    return &incoming_message_;
  }
  void ReleaseHandles() { incoming_message_.num_handles = 0; }
  zx_status_t status() const { return status_; }
  bool ok() const { return status_ == ZX_OK; }

 private:
  fidl_incoming_msg_t incoming_message_ = {};
  zx_status_t status_ = ZX_ERR_BAD_STATE;

  OutgoingMessage::CopiedBytes buf_bytes_;
  std::unique_ptr<zx_handle_info_t[]> buf_handles_;
};

// Converts an outgoing message to an incoming message.
//
// In doing so, it will make syscalls to fetch rights and type information
// of any provided handles. The caller is responsible for ensuring that
// returned handle rights and object types are checked appropriately.
//
// The returned |OutgoingToIncomingMessageResult| will take ownership over
// handles from the input |OutgoingMessage|.
OutgoingToIncomingMessageResult OutgoingToIncomingMessage(OutgoingMessage& input);

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
