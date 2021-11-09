// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/arena.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/types.h>
#include <lib/sync/completion.h>
#include <lib/zx/event.h>

#include <zxtest/zxtest.h>

class RuntimeTestCase : public zxtest::Test {
 protected:
  // Registers a wait_async request on |ch| and signals |completion| once it
  // is ready for reading.
  static void SignalOnChannelReadable(fdf_handle_t ch, fdf_dispatcher_t* dispatcher,
                                      sync_completion_t* completion);

  // Registers a wait_async request on |ch| and blocks until it is ready for reading.
  static void WaitUntilReadReady(fdf_handle_t ch, fdf_dispatcher_t* dispatcher);

  // Reads a message from |ch| and asserts that it matches the wanted parameters.
  // If |out_arena| is provided, it will be populated with the transferred arena.
  static void AssertRead(fdf_handle_t ch, void* want_data, size_t want_num_bytes,
                         zx_handle_t* want_handles, uint32_t want_num_handles,
                         fdf_arena_t** out_arena = nullptr);

  // Returns a fake driver pointer that can be used with driver_context APIs.
  // Do not try to access the internals of the pointer.
  const void* CreateFakeDriver() {
    // We don't actually need a real pointer.
    int driver = next_driver_;
    next_driver_++;
    return reinterpret_cast<const void*>(driver);
  }

 private:
  int next_driver_ = 1;
};
