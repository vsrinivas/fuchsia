// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/txn_header.h>
#include <zircon/assert.h>

#include <memory>

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

}  // namespace internal

// Abstract class representing a FIDL message on the write path.
// Create instances of base classes |OutgoingByteMessage| or |OutgoingIovecMessage|.
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

  // Set the txid in the message header.
  // Requires that there are sufficient bytes to store the header:
  // - in the buffer (OutgoingByteMessage)
  // - in the first iovec buffer (OutgoingIovecMessage)
  virtual void set_txid(zx_txid_t txid) = 0;

  zx_handle_disposition_t* handles() const {
    // Used in destructor so handled via switch rather than virtual function.
    switch (message_.type) {
      case FIDL_OUTGOING_MSG_TYPE_BYTE:
        return message_.byte.handles;
      case FIDL_OUTGOING_MSG_TYPE_IOVEC:
        return message_.iovec.handles;
      default:
        ZX_PANIC("unhandled message type");
    }
  }
  uint32_t handle_actual() const {
    // Used in destructor so handled via switch rather than virtual function.
    switch (message_.type) {
      case FIDL_OUTGOING_MSG_TYPE_BYTE:
        return message_.byte.num_handles;
      case FIDL_OUTGOING_MSG_TYPE_IOVEC:
        return message_.iovec.num_handles;
      default:
        ZX_PANIC("unhandled message type");
    }
  }
  fidl_outgoing_msg_t* message() { return &message_; }
  const fidl_outgoing_msg_t* message() const { return &message_; }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  virtual void ReleaseHandles() = 0;

  // Encodes the data into:
  // - the buffer after linearizing (OutgoingByteMessage)
  // - the iovec array (OutgoingIovecMessage)
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
    CallImpl(FidlType::Type, client_end.channel(), result_bytes, result_capacity, deadline);
  }

  // For asynchronous clients, writes a request.
  ::fidl::Result Write(::fidl::internal::ClientBase* client,
                       ::fidl::internal::ResponseContext* context);
#endif

 protected:
  OutgoingMessage(fidl_outgoing_msg_t msg, uint32_t handle_capacity)
      : ::fidl::Result(ZX_OK, nullptr), message_(msg), handle_capacity_(handle_capacity) {}

  virtual void EncodeImpl(const fidl_type_t* message_type, void* data) = 0;
#ifdef __Fuchsia__
  virtual void WriteImpl(zx_handle_t channel) = 0;
  virtual void CallImpl(const fidl_type_t* response_type, zx_handle_t channel,
                        uint8_t* result_bytes, uint32_t result_capacity, zx_time_t deadline) = 0;
#endif

  uint32_t handle_capacity() const { return handle_capacity_; }

 private:
  fidl_outgoing_msg_t message_;
  uint32_t handle_capacity_;
};

// Class representing an outgoing message backed by a byte array.
// Each instantiation of the class should only be used for one message.
class OutgoingByteMessage final : public OutgoingMessage {
 public:
  // Creates an object which can manage a FIDL message that is backed by a byte array.
  // |bytes| and |handles| will be used as the destination to linearize and encode the message.
  // At this point, the data within |bytes_| and |handles_| is undefined.
  OutgoingByteMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                      zx_handle_disposition_t* handles, uint32_t handle_capacity,
                      uint32_t handle_actual);
  OutgoingByteMessage() = delete;

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message()->byte.bytes); }
  uint32_t byte_actual() const { return message()->byte.num_bytes; }
  void ReleaseHandles() override { message()->byte.num_handles = 0; }

 private:
  // Linearizes and encodes a message. |data| is a pointer to a buffer which holds the source
  // message body which type is defined by |message_type|.
  // If this function succeed:
  // - |status_| is ZX_OK
  // - |message_| holds the data for the linearized version of |data|.
  // If this function fails:
  // - |status_| is not ZX_OK
  // - |error_message_| holds an explanation of the failure
  // - |message_| is undefined
  // The handles in the message are always consumed by LinearizeAndEncode. If it succeeds they will
  // be transferred into |handles_|. If it fails, some handles may be transferred into |handles_|
  // and the rest will be closed.
  void EncodeImpl(const fidl_type_t* message_type, void* data) override;

#ifdef __Fuchsia__
  void WriteImpl(zx_handle_t channel) override;
  void CallImpl(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
                uint32_t result_capacity, zx_time_t deadline) override;
#endif

  void set_txid(zx_txid_t txid) override {
    ZX_ASSERT(byte_actual() >= sizeof(fidl_message_header_t));
    reinterpret_cast<fidl_message_header_t*>(bytes())->txid = txid;
  }

  uint32_t byte_capacity_;
};

// Class representing an outgoing message backed by an iovec array.
// Each instantiation of the class should only be used for one message.
class OutgoingIovecMessage final : public OutgoingMessage {
 public:
  struct constructor_args {
    zx_channel_iovec_t* iovecs;
    uint32_t iovecs_actual;
    uint32_t iovecs_capacity;

    fidl_iovec_substitution_t* substitutions;
    uint32_t substitutions_actual;
    uint32_t substitutions_capacity;

    zx_handle_disposition_t* handles;
    uint32_t handle_actual;
    uint32_t handle_capacity;
  };
  // Creates an object which can manage a FIDL message that is backed by an iovec array.
  // |args.iovecs| and |args.handles| will be used as the destination to encode the message.
  // |args.substitutions| will back the array of patches needed to restore the encoded
  // object after encoding.
  // At this point, the data within |message_.iovec.iovecs|, |message_.iovec.substitutions|
  // and |handles_| is undefined.
  explicit OutgoingIovecMessage(constructor_args args);
  OutgoingIovecMessage() = delete;
  ~OutgoingIovecMessage();

  zx_channel_iovec* iovecs() const { return message()->iovec.iovecs; }
  uint32_t iovec_actual() const { return message()->iovec.num_iovecs; }
  void ReleaseHandles() override { message()->iovec.num_handles = 0; }

 private:
  // Encodes the message to an iovec array.
  void EncodeImpl(const fidl_type_t* message_type, void* data) override;

#ifdef __Fuchsia__
  void WriteImpl(zx_handle_t channel) override;
  void CallImpl(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
                uint32_t result_capacity, zx_time_t deadline) override;
#endif

  void PatchSubstitutions();

  void set_txid(zx_txid_t txid) override {
    // For simplicity, assume that the message header fits in the first iovec.
    ZX_ASSERT(iovec_actual() > 0 && iovecs()[0].capacity >= sizeof(fidl_message_header_t));
    reinterpret_cast<fidl_message_header_t*>(const_cast<void*>(iovecs()[0].buffer))->txid = txid;
  }

  uint32_t iovecs_capacity_;

  fidl_iovec_substitution_t* substitutions_;
  uint32_t substitutions_capacity_;
  uint32_t substitutions_actual_;
};

namespace internal {

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

// This class owns a message of |FidlType| and encodes the message automatically upon construction.
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

// Converts an outgoing message to an incoming message and stores the
// result alongside needed buffers.
//
// In doing so, it will make syscalls to fetch rights and type information
// of any provided handles. The caller is responsible for ensuring that
// outputted handle rights and object types are checked appropriately.
//
// |OutgoingToIncomingMessage| must be in scope when |incoming_message|
// is referenced, because buffers used by |incoming_message| are held by
// |OutgoingToIncomingMessage|.
class OutgoingToIncomingMessage {
 public:
  explicit OutgoingToIncomingMessage(OutgoingMessage& outgoing_msg);
  fidl_incoming_msg_t* incoming_message() {
    ZX_DEBUG_ASSERT(ok());
    return &incoming_message_;
  }
  zx_status_t status() const { return status_; }
  bool ok() const { return status_ == ZX_OK; }

 private:
  fidl_incoming_msg_t incoming_message_ = {};
  zx_status_t status_ = ZX_ERR_BAD_STATE;

  std::unique_ptr<uint8_t[]> buf_bytes_;
  std::unique_ptr<zx_handle_info_t[]> buf_handles_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
