// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/socket.h>
#include <functional>

#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/socket_watcher.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"

namespace debug_ipc {

// An adapter that converts a zx::socket to a StreamBuffer.
class BufferedZxSocket : public SocketWatcher, public StreamBuffer::Writer {
 public:
  using DataAvailableCallback = std::function<void()>;

  BufferedZxSocket();
  ~BufferedZxSocket();

  // A MessageLoopZircon must be already set up on the current thread.
  //
  // Returns true on success.
  bool Init(zx::socket socket);

  void set_data_available_callback(DataAvailableCallback cb) { callback_ = cb; }

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

 private:
  // SocketWatcher implementation.
  void OnSocketReadable(zx_handle_t) override;
  void OnSocketWritable(zx_handle_t) override;

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  zx::socket socket_;
  StreamBuffer stream_;
  MessageLoop::WatchHandle watch_handle_;
  DataAvailableCallback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferedZxSocket);
};

}  // namespace debug_ipc
