// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_FAKE_INLINE_8BIT_COUNTERS_H_
#define SRC_LIB_FUZZING_FIDL_TEST_FAKE_INLINE_8BIT_COUNTERS_H_

#include <lib/sync/completion.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <memory>
#include <thread>

namespace fuzzing {

// Fake inline 8-bit counters. This memory region would normally be provided by LLVM
// instrumentation.
class FakeInline8BitCounters final {
 public:
  ~FakeInline8BitCounters();

  // Writes the given data to the fake counters
  static zx_status_t Write(const uint8_t *data, size_t size) {
    return GetInstance()->WriteImpl(data, size);
  }

  // Returns the bytes at the given offset, or 0 if out of range.
  static uint8_t At(size_t offset) { return GetInstance()->AtImpl(offset); }

  // Resets the object to its initial state. Returns ZX_OK if complete, or ZX_ERR_TIMED_OUT if the
  // caller should drive the dispatcher loop and call again.
  static zx_status_t Reset() { return GetInstance()->ResetImpl(); }

 private:
  static FakeInline8BitCounters *GetInstance();
  FakeInline8BitCounters();

  zx_status_t WriteImpl(const uint8_t *data, size_t size);
  uint8_t AtImpl(size_t offset);
  zx_status_t ResetImpl(zx_duration_t timeout = ZX_MSEC(10));

  // The fake counters.
  std::unique_ptr<uint8_t[]> data_;

  // Used to make |Reset| asynchronous.
  sync_completion_t sync_;
  std::thread resetter_;
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_FAKE_INLINE_8BIT_COUNTERS_H_
