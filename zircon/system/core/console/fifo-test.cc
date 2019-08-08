// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fifo.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>

#include <zxtest/zxtest.h>

namespace {

bool IsReadable(const zx::eventpair& event) {
  zx_status_t status =
      event.wait_one(::llcpp::fuchsia::io::DEVICE_SIGNAL_READABLE, zx::time(), nullptr);
  return status == ZX_OK;
}

TEST(FifoTestCase, EmptyRead) {
  zx::eventpair fifo_event, remote;
  ASSERT_OK(zx::eventpair::create(0, &fifo_event, &remote));

  Fifo fifo(std::move(remote));

  // Should not be signaled as readable
  ASSERT_FALSE(IsReadable(fifo_event));

  uint8_t buffer[16];
  size_t actual = 100;
  ASSERT_STATUS(fifo.Read(buffer, sizeof(buffer), &actual), ZX_ERR_SHOULD_WAIT);
  ASSERT_EQ(actual, 0);
}

TEST(FifoTestCase, SomeData) {
  zx::eventpair fifo_event, remote;
  ASSERT_OK(zx::eventpair::create(0, &fifo_event, &remote));

  Fifo fifo(std::move(remote));
  uint8_t buffer[16] = {};
  for (size_t i = 0; i < sizeof(buffer); ++i) {
    buffer[i] = static_cast<uint8_t>(i + 1);
  }
  size_t actual = 100;
  ASSERT_STATUS(fifo.Write(buffer, sizeof(buffer), &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer));

  // Should be readable now
  ASSERT_TRUE(IsReadable(fifo_event));

  // Read all but the last byte
  uint8_t buffer2[sizeof(buffer)] = {};
  ASSERT_STATUS(fifo.Read(buffer2, sizeof(buffer2) - 1, &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer2) - 1);
  ASSERT_BYTES_EQ(buffer, buffer2, actual);

  // Should be readable still
  ASSERT_TRUE(IsReadable(fifo_event));

  // Read the last byte
  ASSERT_STATUS(fifo.Read(buffer2, sizeof(buffer2), &actual), ZX_OK);
  ASSERT_EQ(actual, 1);
  ASSERT_EQ(buffer2[0], buffer[sizeof(buffer) - 1]);

  // Should not be readable now
  ASSERT_FALSE(IsReadable(fifo_event));
}

TEST(FifoTestCase, Fill) {
  zx::eventpair fifo_event, remote;
  ASSERT_OK(zx::eventpair::create(0, &fifo_event, &remote));

  Fifo fifo(std::move(remote));

  // Do this twice to try to catch bookkeeping errors
  for (size_t j = 0; j < 2; ++j) {
    uint8_t buffer[Fifo::kFifoSize + 1] = {};
    for (size_t i = 0; i < sizeof(buffer); ++i) {
      buffer[i] = static_cast<uint8_t>(j * Fifo::kFifoSize / 2 + i + 1);
    }
    size_t actual = 100;
    ASSERT_STATUS(fifo.Write(buffer, sizeof(buffer), &actual), ZX_OK);
    // We should end up short one byte
    ASSERT_EQ(actual, Fifo::kFifoSize);

    // Should be readable now
    ASSERT_TRUE(IsReadable(fifo_event));

    // Read all back out
    uint8_t buffer2[sizeof(buffer)] = {};
    ASSERT_STATUS(fifo.Read(buffer2, sizeof(buffer2), &actual), ZX_OK);
    ASSERT_EQ(actual, Fifo::kFifoSize);
    ASSERT_BYTES_EQ(buffer, buffer2, actual);

    // Should not be readable now
    ASSERT_FALSE(IsReadable(fifo_event));
  }
}

TEST(FifoTestCase, Wrapping) {
  zx::eventpair fifo_event, remote;
  ASSERT_OK(zx::eventpair::create(0, &fifo_event, &remote));

  Fifo fifo(std::move(remote));

  uint8_t buffer[Fifo::kFifoSize] = {};
  for (size_t i = 0; i < sizeof(buffer); ++i) {
    buffer[i] = static_cast<uint8_t>(i + 1);
  }
  size_t actual = 100;
  ASSERT_STATUS(fifo.Write(buffer, sizeof(buffer), &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer));

  // Read half back out
  uint8_t buffer2[sizeof(buffer) / 2] = {};
  ASSERT_STATUS(fifo.Read(buffer2, sizeof(buffer2), &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer2));
  ASSERT_BYTES_EQ(buffer, buffer2, actual);

  size_t remaining = sizeof(buffer) - sizeof(buffer2);

  // Fill the fifo back up
  for (size_t i = 0; i < sizeof(buffer2); ++i) {
    buffer[i] = static_cast<uint8_t>(3 * i + 1);
  }
  ASSERT_STATUS(fifo.Write(buffer, sizeof(buffer2), &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer2));

  // Read the rest back out
  uint8_t buffer3[sizeof(buffer)] = {};
  ASSERT_STATUS(fifo.Read(buffer3, sizeof(buffer3), &actual), ZX_OK);
  ASSERT_EQ(actual, sizeof(buffer3));
  ASSERT_BYTES_EQ(buffer + sizeof(buffer2), buffer3, remaining);
  ASSERT_BYTES_EQ(buffer, buffer3 + remaining, sizeof(buffer2));

  // Should not be readable now
  ASSERT_FALSE(IsReadable(fifo_event));
}

}  // namespace
