// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debug_commands.h"

#include "garnet/lib/debug_ipc/stream_buffer.h"

void HandleDebugCommandData(StreamBuffer* stream) {
  uint32_t message_size;
  size_t bytes_read = stream->Peek(
      reinterpret_cast<char*>(&message_size), sizeof(message_size));
  if (bytes_read != sizeof(message_size))
    return;  // Don't have enough data for the size header.
  if (!stream->IsAvailable(message_size))
    return;  // Entire message hasn't arrived yet.

  // The message size includes the header.
  std::vector<char> buffer(message_size);
  stream->Read(&buffer[0], message_size);

  // TODO(brettw) dispatch this message to the parser.
}
