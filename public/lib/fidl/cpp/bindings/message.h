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

// Message is a holder for the data and handles to be sent over a channel.
// Message owns its data and handles, but a consumer of Message is free to
// mutate the data and handles. The message's data is comprised of a header
// followed by payload.
class Message {
 public:
  Message();
  ~Message();

  void Reset();

  void AllocData(uint32_t num_bytes);
  void AllocUninitializedData(uint32_t num_bytes);

  // Transfers data and handles to |destination|.
  void MoveTo(Message* destination);

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

 private:
  void Initialize();
  void FreeDataAndCloseHandles();

  uint32_t data_num_bytes_;
  internal::MessageData* data_;
  std::vector<zx_handle_t> handles_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Message);
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

// Read a single message from the channel into the supplied |message|. |handle|
// must be valid. |message| must be non-null and empty (i.e., clear of any data
// and handles).
//
// NOTE: The message isn't validated and may be malformed!
zx_status_t ReadMessage(const zx::channel& handle, Message* message);

// Read a single message from the channel and dispatch to the given receiver.
// |handle| must be valid. |receiver| may be null, in which case the read
// message is simply discarded. If |receiver| is not null, then
// |receiver_result| should be non-null, and will be set the receiver's return
// value.
zx_status_t ReadAndDispatchMessage(const zx::channel& handle,
                                   MessageReceiver* receiver,
                                   bool* receiver_result);

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_MESSAGE_H_
