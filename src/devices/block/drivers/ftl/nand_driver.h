// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FTL_NAND_DRIVER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FTL_NAND_DRIVER_H_

#include <fuchsia/hardware/nand/c/fidl.h>
#include <inttypes.h>
#include <lib/ftl/ndm-driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/protocol/badblock.h>
#include <ddk/protocol/nand.h>

namespace ftl {

// Implementation of the FTL library's driver interface in terms of a device implementing Fuchsia's
// NAND protocol.
class NandDriver : public ftl::NdmBaseDriver {
 public:
  static std::unique_ptr<NandDriver> Create(const nand_protocol_t* parent,
                                            const bad_block_protocol_t* bad_block);

  virtual const fuchsia_hardware_nand_Info& info() const = 0;

  // Cleans all non bad blocks in a given block range. Erase failures are logged amd deemed non
  // fatal.
  virtual void TryEraseRange(uint32_t start_block, uint32_t end_block) = 0;
};

}  // namespace ftl.

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FTL_NAND_DRIVER_H_
