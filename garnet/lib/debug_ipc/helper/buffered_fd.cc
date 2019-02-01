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

  // Register for socket updates from the message loop. Here we assume we're in
  // a writable state already (this will be re-evaluated when we actually try
  // to write) so only need to watch for readable.
  MessageLoop* loop = MessageLoop::Current();
  FXL_DCHECK(loop);
  watch_handle_ = loop->WatchFD(MessageLoop::WatchMode::kRead, fd_.get(), this);
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
    if (num_read == 0) {
      // We asked for data and it had none. Since this assumes async input,
      // that means EOF (otherwise it will return -1 and errno will be EAGAIN).
      OnFDError(fd_.get());
    } else if (num_read == -1) {
      if (errno == EAGAIN) {
        // No data now.
        break;
      } else if (errno == EINTR) {
        // Try again.
        continue;
      } else {
        // Unrecoverable.
        OnFDError(fd_.get());
        return;
      }
    } else if (num_read > 0) {
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

void BufferedFD::OnFDWritable(int fd) {
  // If we get a writable notifications, we know we were registered for
  // read/write update. Go back to only readable watching, if the write buffer
  // is full this will be re-evaluated when the write fails.
  watch_handle_ = MessageLoop::WatchHandle();
  watch_handle_ = MessageLoop::Current()->WatchFD(MessageLoop::WatchMode::kRead,
                                                  fd_.get(), this);
  stream_.SetWritable();
}

void BufferedFD::OnFDError(int fd) {
  watch_handle_ = MessageLoop::WatchHandle();
  fd_.reset();
  if (error_callback_)
    error_callback_();
}

size_t BufferedFD::ConsumeStreamBufferData(const char* data, size_t len) {
  // Loop for handling EINTR.
  ssize_t written;
  while (true) {
    written = write(fd_.get(), data, len);
    if (written == 0) {
      // We asked for data and it had none. Since this assumes async input,
      // that means EOF (otherwise it will return -1 and errno will be EAGAIN).
      OnFDError(fd_.get());
    } else if (written == -1) {
      if (errno == EAGAIN) {
        // Can't write data, fall through to partial write case below.
        written = 0;
      } else if (errno == EINTR) {
        // Try write again.
        continue;
      } else {
        // Unrecoverable.
        OnFDError(fd_.get());
        return 0;
      }
    }
    break;
  }

  if (written < static_cast<ssize_t>(len)) {
    // Partial write, register for updates.
    watch_handle_ = MessageLoop::WatchHandle();
    watch_handle_ = MessageLoop::Current()->WatchFD(
        MessageLoop::WatchMode::kReadWrite, fd_.get(), this);
  }
  return written;
}

}  // namespace debug_ipc
