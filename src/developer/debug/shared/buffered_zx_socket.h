// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_ZX_SOCKET_H_
#define SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_ZX_SOCKET_H_

#include <lib/zx/socket.h>

#include <functional>

#include "src/developer/debug/shared/buffered_stream.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/socket_watcher.h"

namespace debug {

// An adapter that converts a zx::socket to a StreamBuffer.
class BufferedZxSocket final : public BufferedStream, public SocketWatcher {
 public:
  // Constructs a !IsValid() buffered stream not doing anything.
  BufferedZxSocket();

  // Constructs for the given zx::socket. The socket must be valid and a MessageLoop must already
  // have been set up on the current thread.
  //
  // Start() must be called before stream events will be delivered.
  explicit BufferedZxSocket(zx::socket socket);

  ~BufferedZxSocket() final;

  // BufferedStream implementation.
  bool Start() final;
  bool Stop() final;
  bool IsValid() final { return socket_.is_valid(); }

 private:
  // BufferedStream protected implementation.
  void ResetInternal() final;

  // SocketWatcher implementation.
  void OnSocketReadable(zx_handle_t) override;
  void OnSocketWritable(zx_handle_t) override;
  void OnSocketError(zx_handle_t) override;

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  zx::socket socket_;
  MessageLoop::WatchHandle watch_handle_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferedZxSocket);
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_ZX_SOCKET_H_
