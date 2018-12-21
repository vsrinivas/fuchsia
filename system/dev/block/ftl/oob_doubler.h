// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/nand.h>
#include <ddktl/protocol/nand.h>

namespace ftl {

class OobDoubler {
  public:
    explicit OobDoubler(const nand_protocol_t* parent, bool active)
            : parent_(parent), active_(active) {}
    ~OobDoubler() {}

    // Nand protocol interface.
    void Query(zircon_nand_Info* info_out, size_t* nand_op_size_out);
    void Queue(nand_operation_t* operation, nand_queue_callback completion_cb, void* cookie);

  private:
    ddk::NandProtocolClient parent_;
    bool active_;
};

}  // namespace ftl.
