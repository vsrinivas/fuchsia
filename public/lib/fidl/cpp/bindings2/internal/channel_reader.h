// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_INTERNAL_CHANNEL_READER_H_
#define LIB_FIDL_CPP_BINDINGS2_INTERNAL_CHANNEL_READER_H_

#include <async/wait.h>
#include <fidl/cpp/message_buffer.h>
#include <fidl/cpp/message.h>
#include <zx/channel.h>

#include <memory>
#include <utility>
#include <functional>

#include "lib/fidl/cpp/bindings2/internal/message_handler.h"

namespace fidl {
namespace internal {

class ChannelReader {
 public:
  explicit ChannelReader(MessageHandler* message_handler = nullptr);
  ~ChannelReader();

  ChannelReader(const ChannelReader&) = delete;
  ChannelReader& operator=(const ChannelReader&) = delete;

  // Binds the given channel to this |ChannelReader|.
  //
  // The |ChannelReader| will wait asynchronously for messages on this channel
  // and dispatch them to the message handler. After this method returns, the
  // |ChannelReader| will be waiting for incomming messages.
  //
  // If the |ChannelReader| is already bound, the |ChannelReader| will first
  // be unbound.
  zx_status_t Bind(zx::channel channel);

  // Unbinds the channel from this |ChannelReader|.
  //
  // The |ChannelReader| will stop waiting for the messages on this channel.
  //
  // Returns the channel to which this |ChannelReader| was previously bound, if
  // any.
  zx::channel Unbind();

  // Whether the |ChannelReader| is currently bound.
  //
  // See |Bind()| and |Unbind()|.
  bool is_bound() const { return channel_.is_valid(); }

  // The channel to which this |ChannelReader| is bound, if any.
  const zx::channel& channel() const {
    return channel_;
  }

  // Synchronously waits on |channel()| until either a message is available or
  // the peer closes. If the channel is readable, reads a single message from
  // the channel and dispatches it to the message handler.
  //
  // Returns |ZX_ERR_BAD_STATE| if this |ChannelReader| is not bound.
  zx_status_t WaitAndDispatchMessageUntil(zx::time deadline);

  // The given message handler is called whenever the |ChannelReader| reads a
  // message from the channel.
  //
  // The |Message| given to the message handler will be valid until the message
  // handler returns.
  //
  // The handler should return ZX_OK if the message was handled and an error
  // otherwise. If the handler returns ZX_OK, the |ChannelReader| will continue
  // to wait for messages.
  //
  // The handler can destroy the |ChannelReader|, in which case the
  // handler MUST return |ZX_ERR_STOP|. If the handler returns
  // |ZX_ERR_SHOULD_WAIT|, the |ChannelReader| will continue waiting. Other
  // errors cause the |ChannelReader| to unbind from the channel and call the
  // error handler.
  void set_message_handler(MessageHandler* message_handler) {
    message_handler_ = message_handler;
  }

  // The given error handler is called whenever the |ChannelReader| encounters
  // an error on the channel.
  //
  // Before calling the error handler, the |ChannelReader| unbinds the current
  // channel, which means the message handler will not be called after the
  // error handler has been called unless the |ChannelReader| is bound to a new
  // |zx::channel|.
  //
  // The handler can destroy the |ChannelReader|.
  void set_error_handler(std::function<void()> error_handler) {
    error_handler_ = std::move(error_handler);
  }

 private:
  async_wait_result_t OnHandleReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal_t* signal);
  zx_status_t ReadAndDispatchMessage(MessageBuffer* buffer);
  void NotifyError();

  zx::channel channel_;
  async_t* async_;
  async::WaitMethod<ChannelReader, &ChannelReader::OnHandleReady> wait_;
  MessageHandler* message_handler_;
  std::function<void()> error_handler_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_INTERNAL_CHANNEL_READER_H_
