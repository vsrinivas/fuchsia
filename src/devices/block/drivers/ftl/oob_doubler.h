// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FTL_OOB_DOUBLER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FTL_OOB_DOUBLER_H_

#include <fuchsia/hardware/nand/c/banjo.h>
#include <fuchsia/hardware/nand/cpp/banjo.h>

#include <ddk/driver.h>

namespace ftl {

// Automatically doubles the effective OOB size if it's less than 16 bytes.
class OobDoubler {
 public:
  constexpr static uint32_t kThreshold = 16;
  explicit OobDoubler(const nand_protocol_t* parent) : parent_(parent) {}
  ~OobDoubler() {}

  // Nand protocol interface.
  void Query(fuchsia_hardware_nand_Info* info_out, size_t* nand_op_size_out);
  void Queue(nand_operation_t* operation, nand_queue_callback completion_cb, void* cookie);

 private:
  ddk::NandProtocolClient parent_;
  bool active_ = false;
};

}  // namespace ftl.

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FTL_OOB_DOUBLER_H_
