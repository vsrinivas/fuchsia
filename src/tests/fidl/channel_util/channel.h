// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CHANNEL_UTIL_CHANNEL_H_
#define SRC_TESTS_FIDL_CHANNEL_UTIL_CHANNEL_H_

#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <algorithm>
#include <iostream>
#include <utility>

#include "src/tests/fidl/channel_util/bytes.h"

namespace channel_util {

// Non-owning handle types for channel read/write.
using HandleDispositions = std::vector<zx_handle_disposition_t>;
using HandleInfos = std::vector<zx_handle_info_t>;

class Channel {
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

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
    return channel_.wait_one(signal, zx::deadline_after(kTimeoutDuration), nullptr);
  }

  bool is_signal_present(zx_signals_t signal) {
    ZX_ASSERT_MSG(__builtin_popcount(signal) == 1, "wait_for_signal expects exactly 1 signal");
    return ZX_OK == channel_.wait_one(signal, zx::time::infinite_past(), nullptr);
  }

  zx_status_t read_and_check(const Bytes& expected, const HandleInfos& expected_handles = {}) {
    return read_and_check_impl(expected, expected_handles, nullptr);
  }

  zx_status_t read_and_check_unknown_txid(zx_txid_t* out_txid, const Bytes& expected,
                                          const HandleInfos& expected_handles = {}) {
    return read_and_check_impl(expected, expected_handles, out_txid);
  }

  zx::channel& get() { return channel_; }
  void reset() { channel_.reset(); }

 private:
  zx_status_t read_and_check_impl(const Bytes& expected, const HandleInfos& expected_handles,
                                  zx_txid_t* out_unknown_txid) {
    ZX_ASSERT_MSG(0 == expected.size() % 8, "bytes must be 8-byte aligned");
    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t status = channel_.read_etc(0, bytes, handles, std::size(bytes), std::size(handles),
                                           &actual_bytes, &actual_handles);
    if (status != ZX_OK) {
      std::cerr << "read_and_check: channel read() returned status code: " << status << std::endl;
      return status;
    }
    if (out_unknown_txid != nullptr) {
      // If out_unknown_txid is non-null, we need to retrieve the txid.
      if (actual_bytes < sizeof(fidl_message_header_t)) {
        std::cerr << "read_and_check: message body smaller than FIDL message header";
        return ZX_ERR_INVALID_ARGS;
      }
      fidl_message_header_t hdr;
      memcpy(&hdr, bytes, sizeof(fidl_message_header_t));
      *out_unknown_txid = hdr.txid;
    }
    if (expected.size() != actual_bytes) {
      status = ZX_ERR_INVALID_ARGS;
      std::cerr << "read_and_check: num expected bytes: " << expected.size()
                << " num actual bytes: " << actual_bytes << std::endl;
    }
    if (expected_handles.size() != actual_handles) {
      status = ZX_ERR_INVALID_ARGS;
      std::cerr << "read_and_check: num expected handles: " << expected_handles.size()
                << " num actual handles: " << actual_handles << std::endl;
    }
    for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(expected.size()), actual_bytes); i++) {
      constexpr uint32_t kTxidOffset = offsetof(fidl_message_header_t, txid);
      if (out_unknown_txid != nullptr && i >= kTxidOffset &&
          i < kTxidOffset + sizeof(fidl_message_header_t::txid)) {
        // If out_unknown_txid is non-null, the txid value is unknown so it shouldn't be checked.
        continue;
      }
      if (expected.data()[i] != bytes[i]) {
        status = ZX_ERR_INVALID_ARGS;
        std::cerr << std::dec << "read_and_check: bytes[" << i << "] != expected[" << i << "]: 0x"
                  << std::hex << +bytes[i] << " != 0x" << +expected.data()[i] << std::endl;
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
        std::cerr << std::dec << "read_and_check: handles[" << i << "].rights != expected_handles["
                  << i << "].rights: 0x" << std::hex << expected_handles[i].rights << " != 0x"
                  << handles[i].rights << std::endl;
      }
      if (expected_handles[i].type != handles[i].type) {
        status = ZX_ERR_INVALID_ARGS;
        std::cerr << std::dec << "read_and_check: handles[" << i << "].type != expected_handles["
                  << i << "].type: 0x" << std::hex << expected_handles[i].type << " != 0x"
                  << handles[i].type << std::endl;
      }
    }
    return status;
  }

  zx::channel channel_;
};

}  // namespace channel_util

#endif  // SRC_TESTS_FIDL_CHANNEL_UTIL_CHANNEL_H_
