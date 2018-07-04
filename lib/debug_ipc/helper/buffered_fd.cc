// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/buffered_fd.h"

#include <unistd.h>
#include <algorithm>

namespace debug_ipc {

BufferedFD::BufferedFD() {}
BufferedFD::~BufferedFD() {}

bool BufferedFD::Init(fxl::UniqueFD fd) {
  FXL_DCHECK(!fd_.is_valid());  // Can't be initialized more than once.
  fd_ = std::move(fd);
  stream_.set_writer(this);

  // Register for socket updates from the message loop.
  MessageLoop* loop = MessageLoop::Current();
  FXL_DCHECK(loop);
  watch_handle_ =
      loop->WatchFD(MessageLoop::WatchMode::kReadWrite, fd_.get(), this);
  return watch_handle_.watching();
}

void BufferedFD::OnFDReadable(int fd) {
  // Messages from the client to the agent are typically small so we don't need
  // a very large buffer.
  constexpr size_t kBufSize = 1024;

  // Add all available data to the socket buffer.
  while (true) {
    std::vector<char> buffer;
    buffer.resize(kBufSize);

    ssize_t num_read = read(fd_.get(), &buffer[0], kBufSize);
    if (num_read > 0) {
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

void BufferedFD::OnFDWritable(int fd) { stream_.SetWritable(); }

size_t BufferedFD::ConsumeStreamBufferData(const char* data, size_t len) {
  return std::max(0l, write(fd_.get(), data, len));
}

}  // namespace debug_ipc
