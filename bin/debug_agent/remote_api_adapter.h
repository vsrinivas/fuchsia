// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/debug_agent/handle_read_watcher.h"
#include "garnet/public/lib/fxl/macros.h"

namespace debug_ipc {
class StreamBuffer;
}

namespace debug_agent {

class RemoteAPI;

// Converts a raw stream of input data to a series of RemoteAPI calls.
class RemoteAPIAdapter : public HandleReadWatcher {
 public:
  // The stream will be used to read input and send replies back to the
  // client. The pointers must outlive this class (ownership is not taken).
  RemoteAPIAdapter(RemoteAPI* remote_api, debug_ipc::StreamBuffer* stream);

  ~RemoteAPIAdapter();

  RemoteAPI* api() { return api_; }
  debug_ipc::StreamBuffer* stream() { return stream_; }

  // HandleReadWatcher implementation:
  void OnHandleReadable() override;

 private:
  // All pointers are non-owning.
  RemoteAPI* api_;
  debug_ipc::StreamBuffer* stream_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPIAdapter);
};

}  // namespace debug_agent
