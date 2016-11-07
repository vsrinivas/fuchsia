// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/fidl_data_pipe/blocking_copy.h"

#include "lib/ftl/logging.h"
#include "mx/datapipe.h"

#ifndef __Fuchsia__
#error "Fuchsia is the only supported target platform for this code."
#endif

namespace mtl {

bool FidlBlockingCopyFrom(
    mx::datapipe_consumer source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes) {
  for (;;) {
    void* buffer;
    mx_size_t num_bytes = 0;
    mx_status_t result = source.begin_read(
        0u, reinterpret_cast<uintptr_t*>(&buffer), &num_bytes);
    if (result == NO_ERROR) {
      size_t bytes_written = write_bytes(buffer, num_bytes);
      if (bytes_written < num_bytes) {
        FTL_LOG(ERROR) << "write_bytes callback wrote fewer bytes ("
                       << bytes_written << ") written than expected ("
                       << num_bytes
                       << ") in BlockingCopyFrom (pipe closed? out of disk "
                          "space?)";
        // No need to call end_read(), since |source| will be closed.
        return false;
      }
      result = source.end_read(num_bytes);
      if (result != NO_ERROR) {
        FTL_LOG(ERROR) << "end_read error (" << result
                       << ") in BlockingCopyFrom";
        return false;
      }
    } else if (result == ERR_SHOULD_WAIT) {
      result = source.wait_one(MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                               MX_TIME_INFINITE, nullptr);
      if (result != NO_ERROR) {
        // If the producer handle was closed, then treat as EOF.
        return result == ERR_REMOTE_CLOSED;
      }
    } else if (result == ERR_REMOTE_CLOSED) {
      // If the producer handle was closed, then treat as EOF.
      return true;
    } else {
      FTL_LOG(ERROR) << "Unhandled error " << result << " in BlockingCopyFrom";
      // Some other error occurred.
      return false;
    }
  }
}

}  // namespace mtl
