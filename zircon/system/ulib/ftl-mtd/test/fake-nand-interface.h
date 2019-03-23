// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <memory>

#include <lib/mtd/nand-interface.h>

namespace ftl_mtd {

class FakeNandInterface : public mtd::NandInterface {
public:
    FakeNandInterface(uint32_t page_size, uint32_t oob_size, uint32_t block_size, uint32_t size);

    uint32_t PageSize() final;
    uint32_t BlockSize() final;
    uint32_t OobSize() final;
    uint32_t Size() final;
    zx_status_t ReadOob(uint32_t byte_offset, void* bytes) final;
    zx_status_t ReadPage(uint32_t byte_offset, void* bytes, uint32_t* actual) final;
    zx_status_t WritePage(uint32_t byte_offset, const void* data, const void* oob) final;
    zx_status_t EraseBlock(uint32_t byte_offset) final;
    zx_status_t IsBadBlock(uint32_t byte_offset, bool* is_bad_block) final;

    void set_read_actual(uint32_t read_actual) { read_actual_ = read_actual; }
    void set_read_fails(bool read_fails) { read_fails_ = read_fails; }
    void set_write_fails(bool write_fails) { write_fails_ = write_fails; }

private:
    zx_status_t GetPagePointers(uint32_t byte_offset, void** data, void** oob);

    uint32_t read_actual_;
    bool read_fails_;
    bool write_fails_;

    uint32_t page_size_;
    uint32_t oob_size_;
    uint32_t block_size_;
    uint32_t size_;
    std::unique_ptr<uint8_t[]> data_;
};

} // namespace ftl_mtd
