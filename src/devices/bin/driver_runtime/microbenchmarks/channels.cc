// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/channel.h>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "src/devices/bin/driver_runtime/microbenchmarks/assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue a message from a
// Driver Runtime channel, on a single thread.  This does not involve any
// cross-thread wakeups.
bool ChannelWriteReadTest(perftest::RepeatState* state, uint32_t message_size,
                          uint32_t handle_count) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(message_size);

  auto channel_pair = fdf::ChannelPair::Create(0);
  ASSERT_OK(channel_pair.status_value());

  std::string_view tag;
  auto arena = fdf::Arena::Create(0, tag);
  ASSERT_OK(arena.status_value());

  void* data = arena->Allocate(message_size);
  auto handles_buf = static_cast<zx_handle_t*>(arena->Allocate(handle_count * sizeof(zx_handle_t)));

  cpp20::span<zx_handle_t> handles(handles_buf, handle_count);
  for (auto& handle : handles) {
    fdf_handle_t peer;
    ASSERT_OK(fdf_channel_create(0, &handle, &peer));
    // We only need one end of the channel to transfer.
    fdf_handle_close(peer);
  }

  while (state->KeepRunning()) {
    auto status = channel_pair->end0.Write(0, *arena, data, message_size, std::move(handles));
    ASSERT_OK(status.status_value());
    state->NextStep();
    auto read_return = channel_pair->end1.Read(0);
    ASSERT_OK(read_return.status_value());
    data = read_return->data;
    handles = std::move(read_return->handles);
  }

  for (auto& handle : handles) {
    fdf_handle_close(handle);
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
  static const unsigned kHandleCounts[] = {
      0,
      1,
  };
  for (auto message_size : kMessageSizesInBytes) {
    for (auto handle_count : kHandleCounts) {
      auto write_read_name =
          fbl::StringPrintf("Channel/WriteRead/%ubytes/%uhandles", message_size, handle_count);
      perftest::RegisterTest(write_read_name.c_str(), ChannelWriteReadTest, message_size,
                             handle_count);
    }
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
