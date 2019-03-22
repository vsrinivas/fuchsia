// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/socket.h>
#include <functional>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/socket_watcher.h"
#include "src/developer/debug/shared/stream_buffer.h"

namespace debug_ipc {

// An adapter that converts a zx::socket to a StreamBuffer.
class BufferedZxSocket : public SocketWatcher, public StreamBuffer::Writer {
 public:
  using DataAvailableCallback = std::function<void()>;
  using ErrorCallback = std::function<void()>;

  BufferedZxSocket();
  ~BufferedZxSocket();

  // This won't start listening on the socket (some users might want to delay
  // doing that).
  //
  // If successful, it will leave the object in a valid state.
  zx_status_t Init(zx::socket socket);

  // A MessageLoopZircon must be already set up on the current thread.
  // Start can be called as long as valid() is true. ZX_ERR_BAD_STATE will be
  // returned otherwise.
  zx_status_t Start();
  zx_status_t Stop();
  // Stops and leaves the buffer in an invalid state.
  void Reset();

  bool valid() const { return socket_.is_valid(); }

  void set_data_available_callback(DataAvailableCallback cb) { callback_ = cb; }
  void set_error_callback(ErrorCallback cb) { error_callback_ = cb; }

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

 private:
  // SocketWatcher implementation.
  void OnSocketReadable(zx_handle_t) override;
  void OnSocketWritable(zx_handle_t) override;
  void OnSocketError(zx_handle_t) override;

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  zx::socket socket_;
  StreamBuffer stream_;
  MessageLoop::WatchHandle watch_handle_;

  DataAvailableCallback callback_;
  ErrorCallback error_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferedZxSocket);
};

}  // namespace debug_ipc
