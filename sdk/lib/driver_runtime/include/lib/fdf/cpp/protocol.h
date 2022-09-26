// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_PROTOCOL_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_PROTOCOL_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/token.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

namespace fdf {

// Holds context for a registered protocol which is pending a connection request.
//
// After successfully registering the protocol, the client is responsible for retaining
// the structure in memory (and unmodified) until the connect handler runs.
// Thereafter, the protocol may be registered again or destroyed.
//
// This class must only be accessed on the dispatch thread since it lacks internal
// synchronization of its state.
class Protocol : public fdf_token_t {
 public:
  // Handles the connection request to |protocol|. If |status| is ZX_OK, transfers
  // ownership of |channel|.
  //
  // The status is |ZX_OK| if the connection has been established.
  // The status is |ZX_ERR_CANCELED| if the dispatcher was shut down before the
  // connection was made, or the peer token handle has been closed.
  using Handler = fit::function<void(fdf_dispatcher_t* dispatcher, fdf::Protocol* protocol,
                                     zx_status_t status, fdf::Channel channel)>;

  explicit Protocol(Handler handler);

  ~Protocol();

  // Protocol cannot be moved or copied.
  Protocol(const Protocol&) = delete;
  Protocol(Protocol&&) = delete;
  Protocol& operator=(const Protocol&) = delete;
  Protocol& operator=(Protocol&&) = delete;

  // Registers a protocol for |token|.
  //
  // The connect handler will be scheduled to be called on the dispatcher when a client tries
  // to connect with the channel peer of |token|. If the connection has already been requested,
  // the handler will be scheduled immediately.
  //
  // Transfers ownership of |token| to the runtime.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |token| is not a valid channel handle.
  //
  // ZX_ERR_INVALID_ARGS: |handler| or |dispatcher| is NULL.
  //
  // ZX_ERR_BAD_STATE|: The dispatcher is shutting down, or if this
  // has already registered a protocol.
  zx_status_t Register(zx::channel token, fdf_dispatcher_t* dispatcher);

  bool is_pending() { return dispatcher_ != nullptr; }

 private:
  static void CallHandler(fdf_dispatcher_t* dispatcher, fdf_token_t* protocol, zx_status_t status,
                          fdf_handle_t channel);

  Handler handler_;
  fdf_dispatcher_t* dispatcher_ = nullptr;
};

// Connects to the runtime protocol which was, or will be registered with the channel peer of
// |token|. |channel| may be closed by the parent to terminate the connection.
//
// Transfers ownership of |token| to the runtime, and ownership of |channel| to the driver
// which registered the protocol.
//
// # Errors:
//
// ZX_ERR_BAD_HANDLE: |token| is not a valid channel handle, or |channel| is not a valid
// FDF channel handle.
//
// ZX_ERR_BAD_STATE: The dispatcher is shutting down.
zx_status_t ProtocolConnect(zx::channel token, fdf::Channel channel);

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_PROTOCOL_H_
