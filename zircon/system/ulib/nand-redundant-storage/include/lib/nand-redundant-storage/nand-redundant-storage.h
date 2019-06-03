// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fbl/macros.h>
#include <lib/mtd/nand-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage-interface.h>

namespace nand_rs {

class NandRedundantStorage : public NandRedundantStorageInterface {
public:
    static std::unique_ptr<NandRedundantStorage> Create(std::unique_ptr<mtd::NandInterface> iface);

    virtual ~NandRedundantStorage() {}

    // NandRedundantStorageInterface interface:
    zx_status_t WriteBuffer(const std::vector<uint8_t>& buffer,
                            uint32_t num_copies,
                            uint32_t* num_copies_written) override;
    zx_status_t ReadToBuffer(std::vector<uint8_t>* out_buffer) override;

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandRedundantStorage);

private:
    NandRedundantStorage(std::unique_ptr<mtd::NandInterface> iface);

    std::unique_ptr<mtd::NandInterface> iface_;
};

} // namespace nand_rs
