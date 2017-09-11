// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_

#include <mx/channel.h>

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/compiler_specific.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace fidl {
namespace internal {

// The Connector class is responsible for performing read/write operations on a
// channel. It writes messages it receives through the MessageReceiver
// interface that it subclasses, and it forwards messages it reads through the
// MessageReceiver interface assigned as its incoming receiver.
//
// NOTE: Channel I/O is non-blocking.
//
class Connector : public MessageReceiver {
 public:
  // The Connector takes ownership of |channel|.
  explicit Connector(mx::channel channel,
                     const FidlAsyncWaiter* waiter = GetDefaultAsyncWaiter());
  ~Connector() override;

  // Sets the receiver to handle messages read from the channel.  The
  // Connector will read messages from the channel regardless of whether or not
  // an incoming receiver has been set.
  void set_incoming_receiver(MessageReceiver* receiver) {
    incoming_receiver_ = receiver;
  }

  // Errors from incoming receivers will force the connector into an error
  // state, where no more messages will be processed. This method is used
  // during testing to prevent that from happening.
  void set_enforce_errors_from_incoming_receiver(bool enforce) {
    enforce_errors_from_incoming_receiver_ = enforce;
  }

  // Sets the error handler to receive notifications when an error is
  // encountered while reading from the channel or waiting to read from the
  // channel.
  void set_connection_error_handler(fxl::Closure error_handler) {
    connection_error_handler_ = std::move(error_handler);
  }

  // Returns true if an error was encountered while reading from the channel or
  // waiting to read from the channel.
  bool encountered_error() const { return error_; }

  // Closes the channel, triggering the error state. Connector is put into a
  // quiescent state.
  void CloseChannel();

  // Releases the channel, not triggering the error state. Connector is put into
  // a quiescent state.
  mx::channel PassChannel();

  // Is the connector bound to a channel?
  bool is_valid() const { return !!channel_; }

  // Waits for the next message on the channel, blocking until one arrives,
  // |timeout| elapses, or an error happens. Returns |true| if a message has
  // been delivered, |false| otherwise.
  // When returning |false| closes the channel, unless the reason for
  // for returning |false| was |MX_ERR_SHOULD_WAIT| or
  // |MX_ERR_TIMED_OUT|.
  // Use |encountered_error| to see if an error occurred.
  bool WaitForIncomingMessage(fxl::TimeDelta timeout);

  // MessageReceiver implementation:
  bool Accept(Message* message) override;

  mx_handle_t handle() const { return channel_.get(); }

 private:
  static void CallOnHandleReady(mx_status_t result,
                                mx_signals_t pending,
                                uint64_t count,
                                void* closure);
  void OnHandleReady(mx_status_t result, mx_signals_t pending, uint64_t count);

  void WaitToReadMore();

  // Returns false if |this| was destroyed during message dispatch.
  FXL_WARN_UNUSED_RESULT bool ReadSingleMessage(mx_status_t* read_result);

  void NotifyError();

  // Cancels any calls made to |waiter_|.
  void CancelWait();

  fxl::Closure connection_error_handler_;
  const FidlAsyncWaiter* waiter_;

  mx::channel channel_;
  MessageReceiver* incoming_receiver_;

  FidlAsyncWaitID async_wait_id_;
  bool error_;
  bool drop_writes_;
  bool enforce_errors_from_incoming_receiver_;

  // If non-null, this will be set to true when the Connector is destroyed.  We
  // use this flag to allow for the Connector to be destroyed as a side-effect
  // of dispatching an incoming message.
  bool* destroyed_flag_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Connector);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_CONNECTOR_H_
