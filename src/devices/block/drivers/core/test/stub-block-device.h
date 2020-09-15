// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_CORE_TEST_FAKE_BLOCK_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_CORE_TEST_FAKE_BLOCK_DEVICE_H_

#include <cstdint>
#include <cstdlib>
#include <zircon/device/block.h>

#include <ddk/protocol/block.h>
#include <ddktl/protocol/block.h>

constexpr uint32_t kBlockSize = 1024;
constexpr uint64_t kBlockCount = 4096;

class StubBlockDevice : public ddk::BlockProtocol<StubBlockDevice> {
 public:
  StubBlockDevice() : proto_({&block_protocol_ops_, this}) {
    info_.block_count = kBlockCount;
    info_.block_size = kBlockSize;
    info_.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
  }

  block_protocol_t* proto() { return &proto_; }
  void SetInfo(const block_info_t* info) { info_ = *info; }

  // BlockProtocol ops implementation.
  // -----------------------------------
  void BlockQuery(block_info_t* info_out, size_t* block_op_size_out) {
    *info_out = info_;
    *block_op_size_out = sizeof(block_op_t);
  }

  void BlockQueue(block_op_t* operation, block_queue_callback completion_cb, void* cookie);
  // -----------------------------------

 private:
  block_protocol_t proto_{};
  block_info_t info_{};
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_CORE_TEST_FAKE_BLOCK_DEVICE_H_
