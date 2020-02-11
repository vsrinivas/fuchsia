// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <memory>

#include <ddk/protocol/badblock.h>
#include <ddk/protocol/nand.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/ftl/ndm-driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace ftl {

// Encapsulates the lower layer TargetFtl-Ndm driver.
class NandDriver : public ftl::NdmBaseDriver {
 public:
  static std::unique_ptr<NandDriver> Create(const nand_protocol_t* parent,
                                            const bad_block_protocol_t* bad_block);

  virtual const fuchsia_hardware_nand_Info& info() const = 0;
};

}  // namespace ftl.
