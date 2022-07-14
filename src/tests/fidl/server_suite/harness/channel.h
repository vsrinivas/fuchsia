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

// Non-owning handle types for channel read/write.
using HandleDispositions = std::vector<zx_handle_disposition_t>;
using HandleInfos = std::vector<zx_handle_info_t>;

class Channel {
 public:
  Channel() = default;
  Channel(Channel&&) = default;
  Channel& operator=(Channel&&) = default;

  explicit Channel(zx::channel channel) : channel_(std::move(channel)) {}

  zx_status_t write(const Bytes& bytes, const HandleDispositions& handle_dispositions = {}) {
    ZX_ASSERT_MSG(0 == bytes.size() % 8, "bytes must be 8-byte aligned");
    return channel_.write_etc(0, bytes.data(), static_cast<uint32_t>(bytes.size()),
                              const_cast<zx_handle_disposition_t*>(handle_dispositions.data()),
                              static_cast<uint32_t>(handle_dispositions.size()));
  }

  zx_status_t wait_for_signal(zx_signals_t signal) {
    ZX_ASSERT_MSG(__builtin_popcount(signal) == 1, "wait_for_signal expects exactly 1 signal");
    return channel_.wait_one(signal, zx::deadline_after(zx::sec(5)), nullptr);
  }

  bool is_signal_present(zx_signals_t signal) {
    ZX_ASSERT_MSG(__builtin_popcount(signal) == 1, "wait_for_signal expects exactly 1 signal");
    return ZX_OK == channel_.wait_one(signal, zx::deadline_after(zx::msec(1)), nullptr);
  }

  zx_status_t read_and_check(const Bytes& expected, const HandleInfos& expected_handles = {}) {
    ZX_ASSERT_MSG(0 == expected.size() % 8, "bytes must be 8-byte aligned");
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status = channel_.read_etc(0, bytes, handles, std::size(bytes), std::size(handles),
                                           &actual_bytes, &actual_handles);
    if (status != ZX_OK) {
      ADD_FAILURE() << "channel read() returned status code: " << status;
      return status;
    }
    if (expected.size() != actual_bytes) {
      ADD_FAILURE() << "num expected bytes: " << expected.size()
                    << " num actual bytes: " << actual_bytes;
      status = ZX_ERR_INVALID_ARGS;
    }
    if (expected_handles.size() != actual_handles) {
      ADD_FAILURE() << "num expected handles: " << expected_handles.size()
                    << " num actual handles: " << actual_handles;
      status = ZX_ERR_INVALID_ARGS;
    }
    for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(expected.size()), actual_bytes); i++) {
      if (expected.data()[i] != bytes[i]) {
        status = ZX_ERR_INVALID_ARGS;
        ADD_FAILURE() << std::dec << "bytes[" << i << "] != expected[" << i << "]: 0x" << std::hex
                      << +bytes[i] << " != 0x" << +expected.data()[i];
      }
    }
    for (uint32_t i = 0;
         i < std::min(static_cast<uint32_t>(expected_handles.size()), actual_handles); i++) {
      // Sanity checks. These should always be true for a handle sent over a channel.
      ZX_ASSERT(ZX_HANDLE_INVALID != handles[i].handle);
      ZX_ASSERT(0 == handles[i].unused);

      // Ensure rights and object type match expectations.
      if (expected_handles[i].rights != handles[i].rights) {
        status = ZX_ERR_INVALID_ARGS;
        ADD_FAILURE() << std::dec << "handles[" << i << "].rights != expected_handles[" << i
                      << "].rights: 0x" << std::hex << expected_handles[i].rights << " != 0x"
                      << handles[i].rights;
      }
      if (expected_handles[i].type != handles[i].type) {
        status = ZX_ERR_INVALID_ARGS;
        ADD_FAILURE() << std::dec << "handles[" << i << "].type != expected_handles[" << i
                      << "].type: 0x" << std::hex << expected_handles[i].type << " != 0x"
                      << handles[i].type;
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
