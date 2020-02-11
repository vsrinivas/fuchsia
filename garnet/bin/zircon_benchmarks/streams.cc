// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/stream.h>
#include <lib/zx/vmo.h>

#include <algorithm>
#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the time taken to write to a zx::stream for various sizes of writes and with various
// length iovecs.
bool StreamWriteAtTest(perftest::RepeatState* state, uint32_t message_size, size_t vector_count) {
  state->SetBytesProcessedPerRun(message_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(std::max<size_t>(PAGE_SIZE, message_size), 0, &vmo));
  size_t content_size = message_size;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_WRITE, vmo, 0, &stream));
  std::vector<char> buffer(message_size);
  zx_cprng_draw(buffer.data(), buffer.size());

  std::vector<zx_iovec_t> vector(vector_count);
  size_t buffer_capacity = buffer.size() / vector_count;
  for (size_t i = 0; i < vector_count; ++i) {
    vector[i].buffer = buffer.data() + i * buffer_capacity;
    vector[i].capacity = buffer_capacity;
  }

  while (state->KeepRunning()) {
    size_t bytes_written;
    ASSERT_OK(stream.writev_at(0, 0, vector.data(), vector_count, &bytes_written));
    ZX_ASSERT(bytes_written == message_size);
  }
  return true;
}

// Measure the time taken to read from a zx::stream for various sizes of reads and with various
// length iovecs.
bool StreamReadAtTest(perftest::RepeatState* state, uint32_t message_size, size_t vector_count) {
  state->SetBytesProcessedPerRun(message_size);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(std::max<size_t>(PAGE_SIZE, message_size), 0, &vmo));
  size_t content_size = message_size;
  ASSERT_OK(vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size)));

  zx::stream stream;
  ASSERT_OK(zx::stream::create(ZX_STREAM_MODE_READ, vmo, 0, &stream));
  std::vector<char> buffer(message_size);
  zx_cprng_draw(buffer.data(), buffer.size());

  ASSERT_OK(vmo.write(buffer.data(), 0, buffer.size()));

  std::vector<zx_iovec_t> vector(vector_count);
  size_t buffer_capacity = buffer.size() / vector_count;
  for (size_t i = 0; i < vector_count; ++i) {
    vector[i].buffer = buffer.data() + i * buffer_capacity;
    vector[i].capacity = buffer_capacity;
  }

  while (state->KeepRunning()) {
    size_t bytes_read;
    ASSERT_OK(stream.readv_at(0, 0, vector.data(), vector_count, &bytes_read));
    ZX_ASSERT(bytes_read == message_size);
  }
  return true;
}

void RegisterTests() {
  static const unsigned kMessageSizesInBytes[] = {
      64,
      1 * 1024,
      32 * 1024,
      64 * 1024,
  };
  for (auto message_size : kMessageSizesInBytes) {
    auto name = fbl::StringPrintf("Stream/WriteAt/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), StreamWriteAtTest, message_size, 1u);
    name = fbl::StringPrintf("Stream/WriteAt<64>/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), StreamWriteAtTest, message_size, 64u);

    name = fbl::StringPrintf("Stream/ReadAt/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), StreamReadAtTest, message_size, 1u);
    name = fbl::StringPrintf("Stream/ReadAt<64>/%ubytes", message_size);
    perftest::RegisterTest(name.c_str(), StreamReadAtTest, message_size, 64u);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
