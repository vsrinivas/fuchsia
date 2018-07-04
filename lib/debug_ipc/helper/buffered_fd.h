// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/helper/stream_buffer.h"
#include "garnet/public/lib/fxl/files/unique_fd.h"

namespace debug_ipc {

class BufferedFD final : public FDWatcher, public StreamBuffer::Writer {
 public:
  using DataAvailableCallback = std::function<void()>;
  using ErrorCallback = std::function<void()>;

  BufferedFD();
  ~BufferedFD();

  // A MessageLoop must already be set up on the current threa.
  //
  // Returns true on success.
  bool Init(fxl::UniqueFD fd);

  void set_data_available_callback(DataAvailableCallback cb) { callback_ = cb; }
  void set_error_callback(ErrorCallback cb) { error_callback_ = cb; }

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

 private:
  // FDWatcher implementation:
  void OnFDReadable(int fd) override;
  void OnFDWritable(int fd) override;
  void OnFDError(int fd) override;

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  fxl::UniqueFD fd_;
  StreamBuffer stream_;
  MessageLoop::WatchHandle watch_handle_;

  DataAvailableCallback callback_;
  ErrorCallback error_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferedFD);
};

}  // namespace debug_ipc
