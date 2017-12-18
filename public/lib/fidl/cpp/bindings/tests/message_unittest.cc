// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace fidl {
namespace {

bool UsedPreallocatedBuf(PreallocMessage* message) {
  uint8_t* start = reinterpret_cast<uint8_t*>(message);
  uint8_t* end = reinterpret_cast<uint8_t*>(message + 1);
  return start <= message->data() && message->data() < end;
}

// Check that PreallocMessage allocates small message buffers on the stack,
// and that its destructor doesn't pass the buffer to free().
TEST(PreallocMessageTest, StackAlloc) {
  const uint32_t kSize = 16;
  PreallocMessage message;
  message.AllocUninitializedData(kSize);
  EXPECT_TRUE(UsedPreallocatedBuf(&message));
  EXPECT_NE(message.data(), nullptr);
  EXPECT_EQ(message.data_num_bytes(), kSize);
}

// Check that PreallocMessage allocates larger message buffers on the heap.
TEST(PreallocMessageTest, HeapAlloc) {
  const uint32_t kSize = 1000;
  PreallocMessage message;
  message.AllocUninitializedData(kSize);
  EXPECT_FALSE(UsedPreallocatedBuf(&message));
  EXPECT_NE(message.data(), nullptr);
  EXPECT_EQ(message.data_num_bytes(), kSize);
}

}  // namespace
}  // namespace fidl
