// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/cpp/message_part.h>
#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_base.h>
#endif
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

namespace fidl {

// Class representing a FIDL message on the write path.
// Each instantiation of the class should only be used for one message.
class OutgoingMessage final : public ::fidl::Result {
 public:
  // Creates an object which can manage a FIDL message. |bytes| and |handles| will be used as the
  // destination to linearize and encode the message. At this point, the data within |bytes_| and
  // |handles_| is undefined.
  OutgoingMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                  zx_handle_t* handles, uint32_t handle_capacity, uint32_t handle_actual);
  explicit OutgoingMessage(const fidl_msg_t* msg)
      : ::fidl::Result(ZX_OK, nullptr),
        message_(*msg),
        byte_capacity_(msg->num_bytes),
        handle_capacity_(msg->num_handles) {}
  // Copy and move is disabled for the sake of avoiding double handle close.
  // It is possible to implement the move operations with correct semantics if they are
  // ever needed.
  OutgoingMessage(const OutgoingMessage&) = delete;
  OutgoingMessage(OutgoingMessage&&) = delete;
  OutgoingMessage& operator=(const OutgoingMessage&) = delete;
  OutgoingMessage& operator=(OutgoingMessage&&) = delete;
  ~OutgoingMessage();

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  zx_handle_t* handles() const { return message_.handles; }
  uint32_t byte_actual() const { return message_.num_bytes; }
  uint32_t handle_actual() const { return message_.num_handles; }
  uint32_t byte_capacity() const { return byte_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  fidl_msg_t* message() { return &message_; }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { message_.num_handles = 0; }

  // Linearizes and encodes a message. |data| is a pointer to a buffer which holds the source
  // message body which type is defined by |FidlType|.
  // If this function succeed:
  // - |status_| is ZX_OK
  // - |message_| holds the data for the linearized version of |data|.
  // If this function fails:
  // - |status_| is not ZX_OK
  // - |error_message_| holds an explanation of the failure
  // - |message_| is undefined
  template <typename FidlType>
  void LinearizeAndEncode(FidlType* data) {
    LinearizeAndEncode(FidlType::Type, data);
  }

#ifdef __Fuchsia__
  // Uses zx_channel_write to write the linearized message.
  // Before calling Write, LinearizeAndEncode must be called.
  void Write(zx_handle_t channel);

  // For requests with a response, uses zx_channel_call to write the linearized message.
  // Before calling Call, LinearizeAndEncode must be called.
  // If the call succeed, |result_bytes| contains the decoded linearized result.
  template <typename FidlType>
  void Call(zx_handle_t channel, uint8_t* result_bytes, uint32_t result_capacity,
            zx_time_t deadline = ZX_TIME_INFINITE) {
    Call(FidlType::Type, channel, result_bytes, result_capacity, deadline);
  }

  // For asynchronous clients, writes a request.
  ::fidl::Result Write(::fidl::internal::ClientBase* client,
                       ::fidl::internal::ResponseContext* context);
#endif

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
  void LinearizeAndEncode(const fidl_type_t* message_type, void* data);

#ifdef __Fuchsia__
  // For requests with a response, uses zx_channel_call to write the linearized message.
  // Before calling Call, LinearizeAndEncode must be called.
  // If the call succeed, |result_bytes| contains the decoded linearized result.
  void Call(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
            uint32_t result_capacity, zx_time_t deadline);
#endif

  fidl_msg_t message_;
  uint32_t byte_capacity_;
  uint32_t handle_capacity_;
};

// Class representing a FIDL message on the read path.
// Each instantiation of the class should only be used for one message.
class IncomingMessage final : public ::fidl::Result {
 public:
  // Creates an object which can manage a FIDL message. Allocated memory is not owned by
  // the |IncomingMessage|, but handles are owned by it and cleaned up when the
  // |IncomingMessage| is destructed.
  IncomingMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual,
                  zx_handle_t* handles, uint32_t handle_capacity, uint32_t handle_actual);
  explicit IncomingMessage(const fidl_msg_t* msg)
      : ::fidl::Result(ZX_OK, nullptr),
        message_(*msg),
        byte_capacity_(msg->num_bytes),
        handle_capacity_(msg->num_handles) {}
  // Copy and move is disabled for the sake of avoiding double handle close.
  // It is possible to implement the move operations with correct semantics if they are
  // ever needed.
  IncomingMessage(const IncomingMessage&) = delete;
  IncomingMessage(IncomingMessage&&) = delete;
  IncomingMessage& operator=(const IncomingMessage&) = delete;
  IncomingMessage& operator=(IncomingMessage&&) = delete;
  ~IncomingMessage();

  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  zx_handle_t* handles() const { return message_.handles; }
  uint32_t byte_actual() const { return message_.num_bytes; }
  uint32_t handle_actual() const { return message_.num_handles; }
  uint32_t byte_capacity() const { return byte_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  fidl_msg_t* message() { return &message_; }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { message_.num_handles = 0; }

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

  fidl_msg_t message_;
  uint32_t byte_capacity_;
  uint32_t handle_capacity_;
};

template <typename FidlType>
using OwnedOutgoingMessage = typename FidlType::OwnedOutgoingMessage;

template <typename FidlType>
using UnownedOutgoingMessage = typename FidlType::UnownedOutgoingMessage;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
