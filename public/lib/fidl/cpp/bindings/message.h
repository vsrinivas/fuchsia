// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_MESSAGE_H_
#define LIB_FIDL_CPP_BINDINGS_MESSAGE_H_

#include <vector>

#include "lib/fidl/cpp/bindings/internal/message_internal.h"
#include "lib/fxl/compiler_specific.h"
#include "lib/fxl/logging.h"

namespace fidl {

// Message represents a Zircon channel message.  It contains the message's
// data and handles.
//
// Message owns the handles and the handle buffer, but it does not own the
// data buffer.  A consumer of a Message instance is free to mutate the
// data and handles, but it cannot deallocate or transfer ownership of the
// data buffer, which is borrowed.
//
// The message data consists of a header followed by payload data.
class Message {
 public:
  Message();
  ~Message();

  uint32_t data_num_bytes() const { return data_num_bytes_; }

  // Access the raw bytes of the message.
  const uint8_t* data() const {
    return reinterpret_cast<const uint8_t*>(data_);
  }
  uint8_t* mutable_data() { return reinterpret_cast<uint8_t*>(data_); }

  // Access the header.
  const internal::MessageHeader* header() const { return &data_->header; }

  uint32_t name() const { return data_->header.name; }
  bool has_flag(uint32_t flag) const { return !!(data_->header.flags & flag); }

  // Access the request_id field (if present).
  bool has_request_id() const { return data_->header.version >= 1; }
  uint64_t request_id() const {
    FXL_DCHECK(has_request_id());
    return static_cast<const internal::MessageHeaderWithRequestID*>(
               &data_->header)
        ->request_id;
  }
  void set_request_id(uint64_t request_id) {
    FXL_DCHECK(has_request_id());
    static_cast<internal::MessageHeaderWithRequestID*>(&data_->header)
        ->request_id = request_id;
  }

  // Access the payload.
  const uint8_t* payload() const {
    return reinterpret_cast<const uint8_t*>(data_) + data_->header.num_bytes;
  }
  uint8_t* mutable_payload() {
    return reinterpret_cast<uint8_t*>(data_) + data_->header.num_bytes;
  }
  uint32_t payload_num_bytes() const {
    FXL_DCHECK(data_num_bytes_ >= data_->header.num_bytes);
    return data_num_bytes_ - data_->header.num_bytes;
  }

  // Access the handles.
  const std::vector<zx_handle_t>* handles() const { return &handles_; }
  std::vector<zx_handle_t>* mutable_handles() { return &handles_; }

  void MoveHandlesFrom(Message* source);

 protected:
  void CloseHandles();

  uint32_t data_num_bytes_ = 0;
  internal::MessageData* data_ = nullptr;
  std::vector<zx_handle_t> handles_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Message);
};

// AllocMessage is like Message, except that it owns the data buffer.
//
// The data buffer will be heap-allocated.  This means that ownership of
// the buffer can be transferred, using MoveFrom().
class AllocMessage : public Message {
 public:
  AllocMessage();
  ~AllocMessage();

  void Reset();

  void AllocData(uint32_t num_bytes);
  void AllocUninitializedData(uint32_t num_bytes);
  void CopyDataFrom(Message* source);

  // Transfers data and handles from |source|.
  void MoveFrom(AllocMessage* source);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(AllocMessage);
};

// PreallocMessage is similar to AllocMessage, except that it uses a
// preallocated data buffer for messages with small amounts of data.
//
// When PreallocMessage is allocated on the stack, the data for a small
// message will be stack-allocated too.  Larger messages will still be
// heap-allocated.
class PreallocMessage : public Message {
 public:
  PreallocMessage() {}
  ~PreallocMessage();

  void AllocUninitializedData(uint32_t num_bytes);

 private:
  uint8_t prealloc_buf_[128];

  FXL_DISALLOW_COPY_AND_ASSIGN(PreallocMessage);
};

class MessageReceiver {
 public:
  virtual ~MessageReceiver() {}

  // The receiver may mutate the given message.  Returns true if the message
  // was accepted and false otherwise, indicating that the message was invalid
  // or malformed.
  virtual bool Accept(Message* message) FXL_WARN_UNUSED_RESULT = 0;
};

class MessageReceiverWithResponder : public MessageReceiver {
 public:
  ~MessageReceiverWithResponder() override {}

  // A variant on Accept that registers a MessageReceiver (known as the
  // responder) to handle the response message generated from the given
  // message. The responder's Accept method may be called during
  // AcceptWithResponder or some time after its return.
  //
  // NOTE: Upon returning true, AcceptWithResponder assumes ownership of
  // |responder| and will delete it after calling |responder->Accept| or upon
  // its own destruction.
  //
  virtual bool AcceptWithResponder(Message* message, MessageReceiver* responder)
      FXL_WARN_UNUSED_RESULT = 0;
};

// A MessageReceiver that is also able to provide status about the state
// of the underlying channel to which it will be forwarding messages
// received via the |Accept()| call.
class MessageReceiverWithStatus : public MessageReceiver {
 public:
  ~MessageReceiverWithStatus() override {}

  // Returns |true| if this MessageReceiver is currently bound to a channel,
  // the channel has not been closed, and the channel has not encountered an
  // error.
  virtual bool IsValid() = 0;
};

// An alternative to MessageReceiverWithResponder for cases in which it
// is necessary for the implementor of this interface to know about the status
// of the channel which will carry the responses.
class MessageReceiverWithResponderStatus : public MessageReceiver {
 public:
  ~MessageReceiverWithResponderStatus() override {}

  // A variant on Accept that registers a MessageReceiverWithStatus (known as
  // the responder) to handle the response message generated from the given
  // message. Any of the responder's methods (Accept or IsValid) may be called
  // during  AcceptWithResponder or some time after its return.
  //
  // NOTE: Upon returning true, AcceptWithResponder assumes ownership of
  // |responder| and will delete it after calling |responder->Accept| or upon
  // its own destruction.
  //
  virtual bool AcceptWithResponder(Message* message,
                                   MessageReceiverWithStatus* responder)
      FXL_WARN_UNUSED_RESULT = 0;
};

// Read a single message from the channel into the supplied |message|. |channel|
// must be valid. |message| must be non-null and empty (i.e., clear of any data
// and handles).
//
// NOTE: The message isn't validated and may be malformed!
zx_status_t ReadMessage(const zx::channel& channel, PreallocMessage* message);

// Read a single message from the channel and dispatch to the given receiver.
// |handle| must be valid. |receiver| may be null, in which case the read
// message is simply discarded. If |receiver| is not null, then
// |receiver_result| should be non-null, and will be set the receiver's return
// value.
zx_status_t ReadAndDispatchMessage(const zx::channel& channel,
                                   MessageReceiver* receiver,
                                   bool* receiver_result);

zx_status_t WriteMessage(const zx::channel& channel,
                         Message* message);

zx_status_t CallMessage(const zx::channel& channel,
                        Message* message,
                        PreallocMessage* response);

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_MESSAGE_H_
