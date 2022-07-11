// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_CHANNEL_H_
#define SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_CHANNEL_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <algorithm>
#include <utility>

#include <gtest/gtest.h>

#include "src/tests/fidl/server_suite/harness/bytes.h"

namespace server_suite {

class Channel {
 public:
  Channel() = default;
  Channel(Channel&&) = default;
  Channel& operator=(Channel&&) = default;

  explicit Channel(zx::channel channel) : channel_(std::move(channel)) {}

  zx_status_t write(const Bytes& bytes) {
    ZX_ASSERT_MSG(0 == bytes.size() % 8, "bytes must be 8-byte aligned");
    return channel_.write(0, bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, 0);
  }

  zx_status_t wait_for_signal(zx_signals_t signal) {
    ZX_ASSERT_MSG(__builtin_popcount(signal) == 1, "wait_for_signal expects exactly 1 signal");
    return channel_.wait_one(signal, zx::deadline_after(zx::sec(5)), nullptr);
  }

  zx_status_t read_and_check(const Bytes& expected) {
    ZX_ASSERT_MSG(0 == expected.size() % 8, "bytes must be 8-byte aligned");
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status =
        channel_.read(0, bytes, nullptr, std::size(bytes), 0, &actual_bytes, &actual_handles);
    if (status != ZX_OK) {
      ADD_FAILURE() << "channel read() returned status code: " << status;
      return status;
    }
    if (expected.size() != actual_bytes) {
      ADD_FAILURE() << "num expected bytes: " << expected.size()
                    << " num actual bytes: " << actual_bytes;
      status = ZX_ERR_INVALID_ARGS;
    }
    if (0u != actual_handles) {
      ADD_FAILURE() << "num expected handles: " << 0u << " num actual handles: " << actual_handles;
      status = ZX_ERR_INVALID_ARGS;
    }
    for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(expected.size()), actual_bytes); i++) {
      if (expected.data()[i] != bytes[i]) {
        status = ZX_ERR_INVALID_ARGS;
        ADD_FAILURE() << "bytes[" << i << "] != expected[" << i << "]: " << bytes[i]
                      << " != " << expected.data()[i];
      }
    }
    return status;
  }

  zx::channel& get() { return channel_; }
  void reset() { channel_.reset(); }

 private:
  zx::channel channel_;
};

}  // namespace server_suite

#endif  // SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_CHANNEL_H_
