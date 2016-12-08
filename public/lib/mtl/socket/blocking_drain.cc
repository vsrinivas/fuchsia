// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/socket/blocking_drain.h"

#include "lib/ftl/logging.h"
#include "mx/socket.h"

namespace mtl {

bool BlockingDrainFrom(
    mx::socket source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes) {
  for (;;) {
    char buffer[64 * 1024];
    size_t bytes_read;
    mx_status_t result = source.read(0, buffer, sizeof(buffer), &bytes_read);
    if (result == NO_ERROR) {
      size_t bytes_written = write_bytes(buffer, bytes_read);
      if (bytes_written < bytes_read) {
        FTL_LOG(ERROR) << "write_bytes callback wrote fewer bytes ("
                       << bytes_written << ") than expected (" << bytes_read
                       << ") in BlockingDrainFrom (socket closed? out of disk "
                          "space?)";
        return false;
      }
    } else if (result == ERR_SHOULD_WAIT) {
      result = source.wait_one(MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                               MX_TIME_INFINITE, nullptr);
      if (result != NO_ERROR) {
        // If the socket was closed, then treat as EOF.
        return result == ERR_REMOTE_CLOSED;
      }
    } else if (result == ERR_REMOTE_CLOSED) {
      // If the socket was closed, then treat as EOF.
      return true;
    } else {
      FTL_LOG(ERROR) << "Unhandled error " << result << " in BlockingDrainFrom";
      // Some other error occurred.
      return false;
    }
  }
}

}  // namespace mtl
