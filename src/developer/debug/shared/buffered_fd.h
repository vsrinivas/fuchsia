// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_FD_H_
#define SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_FD_H_

#include <fbl/unique_fd.h>

#include "src/developer/debug/shared/buffered_stream.h"
#include "src/developer/debug/shared/message_loop.h"

namespace debug {

class BufferedFD final : public BufferedStream {
 public:
  // Constructs a !IsValid() buffered stream not doing anything.
  BufferedFD();

  // Constructs for the given FD. The FD must be valid and a MessageLoop must already have been set
  // up on the current thread.
  //
  // Start() must be called before stream events will be delivered.
  explicit BufferedFD(fbl::unique_fd fd);

  ~BufferedFD() final;

  // BufferedStream implementation.
  bool Start() final;
  bool Stop() final;
  bool IsValid() final { return fd_.is_valid(); }

 private:
  // BufferedStream protected implementation.
  void ResetInternal() final;

  // FDWatcher.
  void OnFDReady(int fd, bool read, bool write, bool err);

  // Error handler.
  void OnFDError();

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) final;

  fbl::unique_fd fd_;
  MessageLoop::WatchHandle watch_handle_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_BUFFERED_FD_H_
