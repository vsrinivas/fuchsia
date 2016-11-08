// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/legacy_data_pipe/strings.h"

#include <mojo/system/result.h>

#include <utility>

#include "lib/ftl/logging.h"
#include "lib/mtl/legacy_data_pipe/blocking_copy.h"
#include "mojo/public/cpp/system/wait.h"

namespace mtl {

bool LegacyBlockingCopyToString(mojo::ScopedDataPipeConsumerHandle source,
                                std::string* result) {
  FTL_CHECK(result);
  result->clear();
  return LegacyBlockingCopyFrom(
      std::move(source), [result](const void* buffer, uint32_t num_bytes) {
        result->append(static_cast<const char*>(buffer), num_bytes);
        return num_bytes;
      });
}

bool LegacyBlockingCopyFromString(
    ftl::StringView source,
    const mojo::ScopedDataPipeProducerHandle& destination) {
  auto it = source.begin();
  for (;;) {
    void* buffer = nullptr;
    uint32_t buffer_num_bytes = 0;
    MojoResult result =
        mojo::BeginWriteDataRaw(destination.get(), &buffer, &buffer_num_bytes,
                                MOJO_WRITE_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_OK) {
      char* char_buffer = static_cast<char*>(buffer);
      uint32_t byte_index = 0;
      while (it != source.end() && byte_index < buffer_num_bytes)
        char_buffer[byte_index++] = *it++;
      mojo::EndWriteDataRaw(destination.get(), byte_index);
      if (it == source.end())
        return true;
    } else if (result == MOJO_SYSTEM_RESULT_SHOULD_WAIT) {
      result = mojo::Wait(destination.get(), MOJO_HANDLE_SIGNAL_WRITABLE,
                          MOJO_DEADLINE_INDEFINITE, nullptr);
      if (result != MOJO_RESULT_OK) {
        // If the consumer handle was closed, then treat as EOF.
        return result == MOJO_SYSTEM_RESULT_FAILED_PRECONDITION;
      }
    } else {
      // If the consumer handle was closed, then treat as EOF.
      return result == MOJO_SYSTEM_RESULT_FAILED_PRECONDITION;
    }
  }
}

mojo::ScopedDataPipeConsumerHandle LegacyWriteStringToConsumerHandle(
    ftl::StringView source) {
  constexpr size_t max_buffer_size = 2 * 1024 * 1024;  // 2MB
  uint32_t size = source.size();
  FTL_CHECK(size <= max_buffer_size);
  MojoCreateDataPipeOptions options = {sizeof(MojoCreateDataPipeOptions),
                                       MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE,
                                       1, size};
  mojo::DataPipe pipe(options);
  LegacyBlockingCopyFromString(source, std::move(pipe.producer_handle));
  return std::move(pipe.consumer_handle);
}

}  // namespace mtl
