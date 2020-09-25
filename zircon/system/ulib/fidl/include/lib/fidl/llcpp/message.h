// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_MESSAGE_H_
#define LIB_FIDL_LLCPP_MESSAGE_H_

#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

namespace fidl {

// Class used to read/write, encode/decode FIDL messages.
// Each instantiation of the class should only be used for one message.
// However, you can use LinearizedAndEncode and then Decode with the same instance.
class FidlMessage final : public ::fidl::Result {
 public:
  // Creates an object which can manage a FIDL message. |bytes| and |handles| will be used as the
  // destination to linearize and encode the message. At this point, the data within |bytes_| and
  // |handles_| is undefined.
  FidlMessage(uint8_t* bytes, uint32_t byte_capacity, uint32_t byte_actual, zx_handle_t* handles,
              uint32_t handle_capacity, uint32_t handle_actual);
  explicit FidlMessage(const fidl_msg_t* msg)
      : ::fidl::Result(ZX_OK, nullptr),
        message_(*msg),
        byte_capacity_(msg->num_bytes),
        handle_capacity_(msg->num_handles) {}
  ~FidlMessage();

  // The dat is mutable because users can get control of any handle in the decoded buffer.
  uint8_t* bytes() const { return reinterpret_cast<uint8_t*>(message_.bytes); }
  // The encoded handles are fully managed by the class. Users can only have a read access.
  const zx_handle_t* handles() const { return message_.handles; }
  uint32_t byte_actual() const { return message_.num_bytes; }
  uint32_t handle_actual() const { return message_.num_handles; }
  uint32_t byte_capacity() const { return byte_capacity_; }
  uint32_t handle_capacity() const { return handle_capacity_; }
  fidl_msg_t* message() { return &message_; }

  // Release the handles to prevent them to be closed by CloseHandles. This method is only useful
  // when interfacing with low-level channel operations which consume the handles.
  void ReleaseHandles() { message_.num_handles = 0; }

  // Linearizes and encodes a message. |data| is a pointer to a buffer which holds the source
  // message body which type is defined by |message_type|.
  // If this function succeed:
  // - |status_| is ZX_OK
  // - |message_| holds the data for the linearized version of |data|.
  // If this function fails:
  // - |status_| is not ZX_OK
  // - |error_message_| holds an explanation of the failure
  // - |message_| is undefined
  void LinearizeAndEncode(const fidl_type_t* message_type, void* data);

  // Decodes the message using |message_type|. If this operation succeed, |status()| is ok and
  // |bytes()| contains the decoded object.
  // This method should be used after a read.
  void Decode(const fidl_type_t* message_type);

  // Uses zx_channel_write to write the linearized message.
  // Before calling Write, LinearizeAndEncode must be called.
  void Write(zx_handle_t channel);

  // For requests with a response, uses zx_channel_call to write the linearized message.
  // Before calling Call, LinearizeAndEncode must be called.
  // If the call succeed, |result_bytes| contains the decoded linearized result.
  void Call(const fidl_type_t* response_type, zx_handle_t channel, uint8_t* result_bytes,
            uint32_t result_capacity, zx_time_t deadline = ZX_TIME_INFINITE);

  // For asynchronous clients, writes a request.
  ::fidl::Result Write(::fidl::internal::ClientBase* client,
                       ::fidl::internal::ResponseContext* context);

 private:
  fidl_msg_t message_;
  uint32_t byte_capacity_;
  uint32_t handle_capacity_;
};

namespace internal {

// Defines an incomming method entry. Used by a server to dispatch an incoming message.
struct MethodEntry {
  // The ordinal of the method handled by the entry.
  uint64_t ordinal;
  // The coding table of the method (used to decode the message).
  const fidl_type_t* type;
  // The function which handles the decoded message.
  void (*dispatch)(void* interface, void* bytes, ::fidl::Transaction* txn);
};

// The compiler generates an array of MethodEntry for each protocol.
// The TryDispatch method for each protocol calls this function using the generated entries, which
// searches through the array using the method ordinal to find the corresponding dispatch function.
bool TryDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn, MethodEntry* begin,
                 MethodEntry* end);

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_MESSAGE_H_
