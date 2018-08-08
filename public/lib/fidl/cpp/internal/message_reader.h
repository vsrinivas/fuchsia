// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_MESSAGE_READER_H_
#define LIB_FIDL_CPP_INTERNAL_MESSAGE_READER_H_

#include <lib/async/wait.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <memory>
#include <utility>

#include "lib/fidl/cpp/internal/message_handler.h"

namespace fidl {
namespace internal {

class MessageReader {
 public:
  explicit MessageReader(MessageHandler* message_handler = nullptr);
  ~MessageReader();

  MessageReader(const MessageReader&) = delete;
  MessageReader& operator=(const MessageReader&) = delete;

  // Binds the given channel to this |MessageReader|.
  //
  // The |MessageReader| will wait asynchronously for messages on this channel
  // and dispatch them to the message handler using |dispatcher|. After this
  // method returns, the |MessageReader| will be waiting for incomming messages.
  //
  // If the |MessageReader| is already bound, the |MessageReader| will first
  // be unbound.
  //
  // If |dispatcher| is null, the current thread must have a default async_t.
  zx_status_t Bind(zx::channel channel,
                   async_dispatcher_t* dispatcher = nullptr);

  // Unbinds the channel from this |MessageReader|.
  //
  // The |MessageReader| will stop waiting for the messages on this channel.
  //
  // Returns the channel to which this |MessageReader| was previously bound, if
  // any.
  zx::channel Unbind();

  // Unbinds the channel from this |MessageReader| and clears the error handler.
  void Reset();

  // Unbinds the channel from |other| and bindings it to this |MessageReader|.
  //
  // Also moves the error handler from |other| to this |MessageReader|.
  //
  // Useful for implementing move semantics for objects that have a
  // |MessageReader|.
  zx_status_t TakeChannelAndErrorHandlerFrom(MessageReader* other);

  // Whether the |MessageReader| is currently bound.
  //
  // See |Bind()| and |Unbind()|.
  bool is_bound() const { return channel_.is_valid(); }

  // The channel to which this |MessageReader| is bound, if any.
  const zx::channel& channel() const { return channel_; }

  // Synchronously waits on |channel()| until either a message is available or
  // the peer closes. If the channel is readable, reads a single message from
  // the channel and dispatches it to the message handler.
  //
  // Returns |ZX_ERR_BAD_STATE| if this |MessageReader| is not bound.
  zx_status_t WaitAndDispatchOneMessageUntil(zx::time deadline);

  // The given message handler is called whenever the |MessageReader| reads a
  // message from the channel.
  //
  // The |Message| given to the message handler will be valid until the message
  // handler returns.
  //
  // The handler should return ZX_OK if the message was handled and an error
  // otherwise. If the handler returns ZX_OK, the |MessageReader| will continue
  // to wait for messages.
  //
  // The handler can destroy the |MessageReader|, in which case the
  // handler MUST return |ZX_ERR_STOP|. If the handler returns
  // |ZX_ERR_SHOULD_WAIT|, the |MessageReader| will continue waiting. Other
  // errors cause the |MessageReader| to unbind from the channel and call the
  // error handler.
  void set_message_handler(MessageHandler* message_handler) {
    message_handler_ = message_handler;
  }

  // The given error handler is called whenever the |MessageReader| encounters
  // an error on the channel.
  //
  // Before calling the error handler, the |MessageReader| unbinds the current
  // channel, which means the message handler will not be called after the
  // error handler has been called unless the |MessageReader| is bound to a new
  // |zx::channel|.
  //
  // The handler can destroy the |MessageReader|.
  void set_error_handler(fit::closure error_handler) {
    error_handler_ = std::move(error_handler);
  }

 private:
  static void CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                          zx_status_t status, const zx_packet_signal_t* signal);
  void OnHandleReady(async_dispatcher_t* dispatcher, zx_status_t status,
                     const zx_packet_signal_t* signal);
  zx_status_t ReadAndDispatchMessage(MessageBuffer* buffer);
  void NotifyError();
  void Stop();

  async_wait_t wait_;  // Must be first.
  zx::channel channel_;
  async_dispatcher_t* dispatcher_;
  bool* should_stop_;  // See |Canary| in message_reader.cc.
  MessageHandler* message_handler_;
  fit::closure error_handler_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_MESSAGE_READER_H_
