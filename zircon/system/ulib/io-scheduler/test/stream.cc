// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/io-scheduler.h>
#include <zxtest/zxtest.h>

namespace ioscheduler {

TEST(StreamTest, StreamDrain) {
  zx_status_t status;
  Stream stream(5, 0);

  // Insert ops.
  const size_t op_count = 3;
  for (uint32_t i = 0; i < op_count; i++) {
    UniqueOp ref(new StreamOp(OpType::kOpTypeUnknown, 5, kOpGroupNone, 0, nullptr));
    UniqueOp err;
    status = stream.Insert(std::move(ref), &err);
    ASSERT_OK(status, "Failed to insert op");
  }

  status = stream.Close();
  ASSERT_EQ(status, ZX_ERR_SHOULD_WAIT, "Stream closed but not empty");

  for (uint32_t i = 0; i < op_count; i++) {
    UniqueOp ref;
    stream.GetNext(&ref);
    ASSERT_NOT_NULL(ref.get(), "Unexpected null op");

    // Attempt to close stream.
    status = stream.Close();
    ASSERT_EQ(status, ZX_ERR_SHOULD_WAIT, "Stream closed but not empty");

    stream.Complete(ref.get());
    delete ref.release();
  }

  status = stream.Close();
  ASSERT_OK(status, "Stream failed to close");
}

}  // namespace ioscheduler
