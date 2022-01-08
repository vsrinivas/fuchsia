// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/runtime_test_case.h"

// static
void RuntimeTestCase::SignalOnChannelReadable(fdf_handle_t ch, fdf_dispatcher_t* dispatcher,
                                              sync_completion_t* completion) {
  auto channel_read = std::make_unique<fdf::ChannelRead>(
      ch, 0 /* options */,
      [completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                   fdf_status_t status) {
        sync_completion_signal(completion);
        delete channel_read;
      });
  ASSERT_OK(channel_read->Begin(dispatcher));
  channel_read.release();  // Will be deleted on callback.
}

// static
void RuntimeTestCase::WaitUntilReadReady(fdf_handle_t ch, fdf_dispatcher_t* dispatcher) {
  sync_completion_t read_completion;
  ASSERT_NO_FATAL_FAILURE(SignalOnChannelReadable(ch, dispatcher, &read_completion));
  sync_completion_wait(&read_completion, ZX_TIME_INFINITE);
}

// static
void RuntimeTestCase::AssertRead(fdf_handle_t ch, void* want_data, size_t want_num_bytes,
                                 zx_handle_t* want_handles, uint32_t want_num_handles,
                                 fdf_arena_t** out_arena) {
  fdf_arena_t* arena;
  void* read_data;
  uint32_t num_bytes;
  zx_handle_t* handles;
  uint32_t num_handles;
  ASSERT_EQ(ZX_OK, fdf_channel_read(ch, 0, &arena, &read_data, &num_bytes, &handles, &num_handles));

  ASSERT_EQ(num_bytes, want_num_bytes);
  if (want_num_bytes > 0) {
    ASSERT_NOT_NULL(arena);
    ASSERT_TRUE(fdf_arena_contains(arena, read_data, num_bytes));
    ASSERT_EQ(0, memcmp(want_data, read_data, want_num_bytes));
  }
  ASSERT_EQ(num_handles, want_num_handles);
  if (want_num_handles > 0) {
    ASSERT_NOT_NULL(arena);
    ASSERT_TRUE(fdf_arena_contains(arena, handles, num_handles * sizeof(fdf_handle_t)));
    ASSERT_EQ(0, memcmp(want_handles, handles, want_num_handles * sizeof(fdf_handle_t)));
  }
  if (arena) {
    if (out_arena) {
      *out_arena = arena;
    } else {
      fdf_arena_destroy(arena);
    }
  } else {
    ASSERT_NULL(read_data);
    ASSERT_NULL(handles);
  }
}
