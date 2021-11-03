// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/port.h>

#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue a message from a
// Zircon channel, on a single thread.  This does not involve any
// cross-thread wakeups.
bool ChannelWriteReadTest(perftest::RepeatState* state, uint32_t message_size,
                          uint32_t handle_count) {
  state->DeclareStep("write");
  state->DeclareStep("read");
  state->SetBytesProcessedPerRun(message_size);

  zx::channel channel1;
  zx::channel channel2;
  ASSERT_OK(zx::channel::create(0, &channel1, &channel2));
  std::vector<char> buffer(message_size);

  std::vector<zx_handle_t> handles(handle_count);
  for (auto& handle : handles) {
    ASSERT_OK(zx_port_create(0, &handle));
  }

  while (state->KeepRunning()) {
    ASSERT_OK(channel1.write(0, buffer.data(), message_size, handles.data(), handle_count));
    state->NextStep();
    ASSERT_OK(channel2.read(0, buffer.data(), handles.data(), message_size, handle_count, nullptr,
                            nullptr));
  }

  for (auto& handle : handles) {
    ASSERT_OK(zx_handle_close(handle));
  }
  return true;
}

// Measure the times taken to enqueue and then dequeue a message from a
// Zircon channel, on a single thread, using the zx_channel_write_etc and
// zx_channel_read_etc system calls.  This does not involve any
// cross-thread wakeups.
bool ChannelWriteEtcReadEtcTest(perftest::RepeatState* state, uint32_t message_size,
                                uint32_t handle_count) {
  state->DeclareStep("write_etc");
  state->DeclareStep("read_etc");
  state->SetBytesProcessedPerRun(message_size);

  zx::channel channel1;
  zx::channel channel2;
  ASSERT_OK(zx::channel::create(0, &channel1, &channel2));
  std::vector<char> buffer(message_size);

  std::vector<zx_handle_disposition_t> handle_dispositions(handle_count);
  for (auto& handle_disposition : handle_dispositions) {
    handle_disposition.operation = ZX_HANDLE_OP_MOVE;
    handle_disposition.type = ZX_OBJ_TYPE_PORT;
    handle_disposition.rights = ZX_RIGHT_SAME_RIGHTS;
    handle_disposition.result = ZX_OK;
    ASSERT_OK(zx_port_create(0, &handle_disposition.handle));
  }

  std::vector<zx_handle_info_t> handle_info(handle_count);

  while (state->KeepRunning()) {
    ASSERT_OK(channel1.write_etc(0, buffer.data(), message_size, handle_dispositions.data(),
                                 handle_count));
    state->NextStep();
    ASSERT_OK(channel2.read_etc(0, buffer.data(), handle_info.data(), message_size, handle_count,
                                nullptr, nullptr));

    // The original handles are invalid because they were moved. Put the handles that were read in
    // the handle disposition array.
    for (uint32_t i = 0; i < handle_count; i++) {
      handle_dispositions[i].handle = handle_info[i].handle;
    }
  }

  for (auto& handle_disposition : handle_dispositions) {
    ASSERT_OK(zx_handle_close(handle_disposition.handle));
  }
  return true;
}

// Measure the times taken to enqueue and then dequeue a message from a
// Zircon channel, on a single thread, using the zx_channel_write_etc and
// zx_channel_read_etc system calls. This benchmark differs from the other
// benchmarks in this file in that it uses the ZX_CHANNEL_WRITE_USE_IOVEC
// option with zx_channel_write_etc, meaning that the input to
// zx_channel_write_etc is specified as an array of zx_channel_iovec_t rather
// than a byte array. This does not involve any cross-thread wakeups.
bool ChannelWriteEtcReadEtcIovecTest(perftest::RepeatState* state, uint32_t message_size,
                                     uint32_t num_iovecs, uint32_t handle_count) {
  state->DeclareStep("write_etc");
  state->DeclareStep("read_etc");
  state->SetBytesProcessedPerRun(message_size);

  zx::channel channel1;
  zx::channel channel2;
  ASSERT_OK(zx::channel::create(0, &channel1, &channel2));
  std::vector<char> buffer(message_size);
  std::vector<zx_channel_iovec_t> iovecs(num_iovecs);

  uint32_t bytes_per_iovec = message_size / num_iovecs;
  ZX_ASSERT(bytes_per_iovec * num_iovecs == message_size);
  for (uint32_t i = 0; i < num_iovecs; i++) {
    iovecs[i] = zx_channel_iovec_t{
        .buffer = buffer.data() + (i * bytes_per_iovec),
        .capacity = bytes_per_iovec,
    };
  }

  std::vector<zx_handle_disposition_t> handle_dispositions(handle_count);
  for (auto& handle_disposition : handle_dispositions) {
    handle_disposition.operation = ZX_HANDLE_OP_MOVE;
    handle_disposition.type = ZX_OBJ_TYPE_PORT;
    handle_disposition.rights = ZX_RIGHT_SAME_RIGHTS;
    handle_disposition.result = ZX_OK;
    ASSERT_OK(zx_port_create(0, &handle_disposition.handle));
  }

  std::vector<zx_handle_info_t> handle_infos(handle_count);

  while (state->KeepRunning()) {
    ASSERT_OK(channel1.write_etc(ZX_CHANNEL_WRITE_USE_IOVEC, iovecs.data(), num_iovecs,
                                 handle_dispositions.data(), handle_count));
    state->NextStep();
    ASSERT_OK(channel2.read_etc(0, buffer.data(), handle_infos.data(), message_size, handle_count,
                                nullptr, nullptr));
  }

  for (auto& handle_disposition : handle_dispositions) {
    ASSERT_OK(zx_handle_close(handle_disposition.handle));
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

      auto write_etc_read_etc_name = fbl::StringPrintf("Channel/WriteEtcReadEtc/%ubytes/%uhandles",
                                                       message_size, handle_count);
      perftest::RegisterTest(write_etc_read_etc_name.c_str(), ChannelWriteEtcReadEtcTest,
                             message_size, handle_count);
    }
  }

  // Fewer message sizes to use with iovec because of the combinatorial
  // explosion in benchmark cases with 3 parameters.
  static const unsigned kMessageSizesInBytesForIovec[] = {
      64,
      1 * 1024,
      64 * 1024,
  };
  // kIovecChunkSize in message_packet.cc is 16, meaning that iovec count <= 16
  // will use a fast path and store iovecs in a stack buffer.
  static const unsigned kNumIovecs[] = {
      1,
      16,
      32,
      64,
  };
  static const unsigned kNumIovecsWithHandle[] = {
      16,
      64,
  };
  for (auto message_size : kMessageSizesInBytesForIovec) {
    for (auto num_iovecs : kNumIovecs) {
      const unsigned handle_count = 0;
      auto write_etc_read_etc_name =
          fbl::StringPrintf("Channel/WriteEtcReadEtcIovecs/%ubytes/%uiovecs_%ubytes_each/%uhandles",
                            message_size, num_iovecs, message_size / num_iovecs, handle_count);
      perftest::RegisterTest(write_etc_read_etc_name.c_str(), ChannelWriteEtcReadEtcIovecTest,
                             message_size, num_iovecs, handle_count);
    }
    for (auto num_iovecs : kNumIovecsWithHandle) {
      const unsigned handle_count = 1;
      auto write_etc_read_etc_name =
          fbl::StringPrintf("Channel/WriteEtcReadEtcIovecs/%ubytes/%uiovecs_%ubytes_each/%uhandles",
                            message_size, num_iovecs, message_size / num_iovecs, handle_count);
      perftest::RegisterTest(write_etc_read_etc_name.c_str(), ChannelWriteEtcReadEtcIovecTest,
                             message_size, num_iovecs, handle_count);
    }
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
