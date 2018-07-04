// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/buffered_zx_socket.h"

#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "garnet/public/lib/fxl/logging.h"

namespace debug_ipc {

BufferedZxSocket::BufferedZxSocket() = default;
BufferedZxSocket::~BufferedZxSocket() = default;

bool BufferedZxSocket::Init(zx::socket socket) {
  FXL_DCHECK(!socket_.is_valid());  // Can't be initialized more than once.
  socket_ = std::move(socket);
  stream_.set_writer(this);

  // Register for socket updates from the message loop.
  MessageLoopZircon* loop = MessageLoopZircon::Current();
  FXL_DCHECK(loop);
  watch_handle_ = loop->WatchSocket(MessageLoop::WatchMode::kReadWrite,
                                    socket_.get(), this);
  return watch_handle_.watching();
}

void BufferedZxSocket::OnSocketReadable(zx_handle_t) {
  // Messages from the client to the agent are typically small so we don't need
  // a very large buffer.
  constexpr size_t kBufSize = 1024;

  // Add all available data to the socket buffer.
  while (true) {
    std::vector<char> buffer;
    buffer.resize(kBufSize);

    size_t num_read = 0;
    if (socket_.read(0, &buffer[0], kBufSize, &num_read) == ZX_OK) {
      buffer.resize(num_read);
      stream_.AddReadData(std::move(buffer));
    } else {
      break;
    }
    // TODO(brettw) it would be nice to yield here after reading "a bunch" of
    // data so this pipe doesn't starve the entire app.
  }

  if (callback_)
    callback_();
}

void BufferedZxSocket::OnSocketWritable(zx_handle_t) { stream_.SetWritable(); }

size_t BufferedZxSocket::ConsumeStreamBufferData(const char* data, size_t len) {
  size_t written = 0;
  socket_.write(0, data, len, &written);
  return written;
}

}  // namespace debug_ipc
