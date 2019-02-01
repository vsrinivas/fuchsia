// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_REMOTE_API_ADAPTER_H_
#define GARNET_BIN_DEBUG_AGENT_REMOTE_API_ADAPTER_H_

#include "lib/fxl/macros.h"

namespace debug_ipc {
class StreamBuffer;
}

namespace debug_agent {

class RemoteAPI;

// Converts a raw stream of input data to a series of RemoteAPI calls.
class RemoteAPIAdapter {
 public:
  // The stream will be used to read input and send replies back to the
  // client. The creator must set it up so that OnStreamReadable() is called
  // whenever there is new data to read on the stream.
  //
  // The pointers must outlive this class (ownership is not taken).
  RemoteAPIAdapter(RemoteAPI* remote_api, debug_ipc::StreamBuffer* stream);

  ~RemoteAPIAdapter();

  RemoteAPI* api() { return api_; }
  debug_ipc::StreamBuffer* stream() { return stream_; }

  // Callback for when data is available to read on the stream.
  void OnStreamReadable();

 private:
  // All pointers are non-owning.
  RemoteAPI* api_;
  debug_ipc::StreamBuffer* stream_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteAPIAdapter);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_REMOTE_API_ADAPTER_H_
