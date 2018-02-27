// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/stream_buffer.h"
#include "lib/fxl/macros.h"

#ifdef __Fuchsia__
#include <zircon/types.h>
#endif

namespace zxdb {

// Represents the client end of the connection to the system debug agent.
//
// This class only does simple synchronous I/O operations so uses ifdefs to
// manage platform differences. The more complex handling of asynchronous I/O
// and notifying when things are readable and writable is done by the
// MainLoop.
//
// This design supposes only the console debugger. If/when there are debuggers
// that use the client code but don't read from stdin, the message loop code
// doing this will need to be refactored a bit.
class AgentConnection : public debug_ipc::StreamBuffer::Writer {
 public:
  // Receives data from the remote connection.
  class Sink {
   public:
    // Called when there is new data. The implementation need not consume
    // all of it (since there may be partial messages).
    virtual void OnAgentData(debug_ipc::StreamBuffer* stream) = 0;
  };

#ifdef __Fuchsia__
  // Native connection is a socket.
  using NativeHandle = zx_handle_t;
#else
  // Posix file descriptor.
  using NativeHandle = int;
#endif

  // Does NOT take ownership of the sink, which must outlive this class. Takes
  // ownership of the native handle and will close it.
  AgentConnection(Sink* sink, NativeHandle handle);
  virtual ~AgentConnection();

  // Sends the given data to the remote agent.
  void Send(std::vector<char> data);

  NativeHandle native_handle() const { return native_handle_; }

  // Notifications that the native handle has transitioned to a readable or
  // writable state. They both must be able to handle the case when there are
  // zero bytes readable or writable.
  void OnNativeHandleWritable();
  void OnNativeHandleReadable();

 private:
  // StreamBuffer::Writer implementation. Sends data to the agent.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  Sink* const sink_;  // Non-owning.
  NativeHandle native_handle_;  // Owning.

  debug_ipc::StreamBuffer stream_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AgentConnection);
};

}  // namespace zxdb
