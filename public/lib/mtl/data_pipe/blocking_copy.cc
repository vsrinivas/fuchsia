// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/data_pipe/blocking_copy.h"

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/system/wait.h"

#ifndef __Fuchsia__
#error "Fuchsia is the only supported target platform for this code."
#endif

namespace mtl {

bool BlockingCopyFrom(
    mojo::ScopedDataPipeConsumerHandle source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes) {
  for (;;) {
    const void* buffer = nullptr;
    uint32_t num_bytes = 0;
    MojoResult result = mojo::BeginReadDataRaw(
        source.get(), &buffer, &num_bytes, MOJO_READ_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_OK) {
      size_t bytes_written = write_bytes(buffer, num_bytes);
      if (bytes_written < num_bytes) {
        FTL_LOG(ERROR) << "write_bytes callback wrote fewer bytes ("
                       << bytes_written << ") written than expected ("
                       << num_bytes
                       << ") in BlockingCopyFrom (pipe closed? out of disk "
                          "space?)";
        // No need to call EndReadDataRaw(), since |source| will be closed.
        return false;
      }
      result = mojo::EndReadDataRaw(source.get(), num_bytes);
      if (result != MOJO_RESULT_OK) {
        FTL_LOG(ERROR) << "EndReadDataRaw error (" << result
                       << ") in BlockingCopyFrom";
        return false;
      }
    } else if (result == MOJO_RESULT_SHOULD_WAIT) {
      result = mojo::Wait(source.get(), MOJO_HANDLE_SIGNAL_READABLE,
                          MOJO_DEADLINE_INDEFINITE, nullptr);
      if (result != MOJO_RESULT_OK) {
        // If the producer handle was closed, then treat as EOF.
        return result == MOJO_RESULT_FAILED_PRECONDITION;
      }
    } else if (result == MOJO_RESULT_FAILED_PRECONDITION) {
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
