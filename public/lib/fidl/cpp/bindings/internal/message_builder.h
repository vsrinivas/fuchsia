// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These are helper classes for allocating, aligning, and framing a
// |fidl::Message|. They are mainly used from within the generated C++ bindings.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_BUILDER_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_BUILDER_H_

#include <stdint.h>

#include "lib/fidl/cpp/bindings/internal/fixed_buffer.h"
#include "lib/fidl/cpp/bindings/internal/message_internal.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {

class Message;

// MessageBuilder helps initialize and frame a |fidl::Message| that does not
// expect a response message, and therefore does not tag the message with a
// request id (which may save some bytes).
//
// The underlying |Message| is owned by MessageBuilder, but can be permanently
// moved by accessing |message()| and calling its |MoveTo()|.
class MessageBuilder {
 public:
  // This frames and configures a |fidl::Message| with the given message name.
  MessageBuilder(uint32_t name, size_t payload_size);
  ~MessageBuilder();

  Message* message() { return &message_; }

  // TODO(vardhan): |buffer()| is internal and only consumed by internal classes
  // and unittests.  Consider making it private + friend class its consumers?
  internal::Buffer* buffer() { return &buf_; }

 protected:
  MessageBuilder();
  void Initialize(size_t size);

  Message message_;
  internal::FixedBuffer buf_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageBuilder);
};

namespace internal {

class MessageWithRequestIDBuilder : public MessageBuilder {
 public:
  MessageWithRequestIDBuilder(uint32_t name,
                              size_t payload_size,
                              uint32_t flags,
                              uint64_t request_id);
};

}  // namespace internal

// Builds a |fidl::Message| that is a "request" message that expects a response
// message. You can give it a unique |request_id| (with which you can construct
// a response message) by calling |message()->set_request_id()|.
//
// Has the same interface as |fidl::MessageBuilder|.
class RequestMessageBuilder : public internal::MessageWithRequestIDBuilder {
 public:
  RequestMessageBuilder(uint32_t name, size_t payload_size)
      : MessageWithRequestIDBuilder(name,
                                    payload_size,
                                    internal::kMessageExpectsResponse,
                                    0) {}
};

// Builds a |fidl::Message| that is a "response" message which pertains to a
// |request_id|.
//
// Has the same interface as |fidl::MessageBuilder|.
class ResponseMessageBuilder : public internal::MessageWithRequestIDBuilder {
 public:
  ResponseMessageBuilder(uint32_t name,
                         size_t payload_size,
                         uint64_t request_id)
      : MessageWithRequestIDBuilder(name,
                                    payload_size,
                                    internal::kMessageIsResponse,
                                    request_id) {}
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MESSAGE_BUILDER_H_
