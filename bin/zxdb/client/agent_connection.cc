// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/agent_connection.h"

#include <unistd.h>

#ifdef __Fuchsia__
#include "zircon/syscalls.h"
#endif

#include "garnet/bin/zxdb/client/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

AgentConnection::AgentConnection(Sink* sink, NativeHandle handle)
    : sink_(sink), native_handle_(handle) {
  stream_buffer_.set_writer(this);
}

AgentConnection::~AgentConnection() {
#ifdef __Fuchsia__
  zx_handle_close(native_handle_);
#else
  close(native_handle_);
#endif
}

void AgentConnection::OnNativeHandleWritable() {
  // Note: this will reenter us and call ConsumeStreamBufferData().
  stream_buffer_.SetWritable();
}

void AgentConnection::Send(std::vector<char> data) {
  stream_buffer_.Write(std::move(data));
}

void AgentConnection::OnNativeHandleReadable() {
  constexpr size_t kBufSize = 4096;

  bool has_data = false;
  while (true) {
    std::vector<char> buffer;
    buffer.resize(kBufSize);

#ifdef __Fuchsia__
    size_t num_read = 0;
    if (zx_socket_read(native_handle_, 0, &buffer[0], kBufSize, &num_read) !=
        ZX_OK)
      break;
    buffer.resize(num_read);
#else
    ssize_t num_read = read(native_handle_, &buffer[0], kBufSize);
    if (num_read <= 0)
      break;
    buffer.resize(static_cast<size_t>(num_read));
#endif
    has_data = true;
    stream_buffer_.AddReadData(std::move(buffer));
  }

  if (has_data)
    sink_->OnAgentData(&stream_buffer_);
}

size_t AgentConnection::ConsumeStreamBufferData(const char* data, size_t len) {
#ifdef __Fuchsia__
  size_t written = 0;
  zx_socket_write(native_handle_, 0, data, len, &written);
  return written;
#else
  ssize_t written = write(native_handle_, data, len);
  if (written < 0)
    return 0;
  return written;
#endif
}

}  // namespace zxdb
