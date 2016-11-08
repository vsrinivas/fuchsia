// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/data_pipe/strings.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "lib/mtl/data_pipe/blocking_copy.h"

namespace mtl {

bool BlockingCopyToString(mx::datapipe_consumer source, std::string* result) {
  FTL_CHECK(result);
  result->clear();
  return BlockingCopyFrom(
      std::move(source), [result](const void* buffer, uint32_t num_bytes) {
        result->append(static_cast<const char*>(buffer), num_bytes);
        return num_bytes;
      });
}

bool BlockingCopyFromString(ftl::StringView source,
                            const mx::datapipe_producer& destination) {
  auto it = source.begin();
  for (;;) {
    void* buffer = nullptr;
    mx_size_t buffer_num_bytes = 0;
    mx_status_t result = destination.begin_write(
        0u, reinterpret_cast<uintptr_t*>(&buffer), &buffer_num_bytes);
    if (result == NO_ERROR) {
      char* char_buffer = static_cast<char*>(buffer);
      uint32_t byte_index = 0;
      while (it != source.end() && byte_index < buffer_num_bytes)
        char_buffer[byte_index++] = *it++;
      destination.end_write(byte_index);
      if (it == source.end())
        return true;
    } else if (result == ERR_SHOULD_WAIT) {
      result = destination.wait_one(MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED,
                                    MX_TIME_INFINITE, nullptr);
      if (result != NO_ERROR) {
        // If the consumer handle was closed, then treat as EOF.
        return result == ERR_REMOTE_CLOSED;
      }
    } else {
      // If the consumer handle was closed, then treat as EOF.
      return result == ERR_REMOTE_CLOSED;
    }
  }
}

mx::datapipe_consumer WriteStringToConsumerHandle(ftl::StringView source) {
  constexpr size_t max_buffer_size = 2 * 1024 * 1024;  // 2MB
  uint32_t size = source.size();
  FTL_CHECK(size <= max_buffer_size);
  mx::datapipe_producer producer_handle;
  mx::datapipe_consumer consumer_handle;
  mx::datapipe_producer::create(1, size, 0u, &producer_handle,
                                &consumer_handle);
  BlockingCopyFromString(source, std::move(producer_handle));
  return consumer_handle;
}

}  // namespace mtl
