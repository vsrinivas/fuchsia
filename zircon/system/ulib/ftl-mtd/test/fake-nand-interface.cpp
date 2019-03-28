// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include "fake-nand-interface.h"

namespace ftl_mtd {

FakeNandInterface::FakeNandInterface(uint32_t page_size, uint32_t oob_size, uint32_t block_size,
                                     uint32_t size)
    : read_actual_(page_size), fail_read_(false), fail_write_(false), fail_erase_(false),
      fail_is_bad_block_(false), page_size_(page_size), oob_size_(oob_size),
      block_size_(block_size), size_(size) {
    size_t num_blocks = size_ / block_size_;
    bad_blocks_ = std::make_unique<bool[]>(num_blocks);

    size_t data_size = (size_ / page_size_) * (page_size_ + oob_size_);
    data_ = std::make_unique<uint8_t[]>(data_size);
}

uint32_t FakeNandInterface::PageSize() {
    return page_size_;
}
uint32_t FakeNandInterface::BlockSize() {
    return block_size_;
}
uint32_t FakeNandInterface::OobSize() {
    return oob_size_;
}
uint32_t FakeNandInterface::Size() {
    return size_;
}

zx_status_t FakeNandInterface::ReadOob(uint32_t byte_offset, void* bytes) {
    if (fail_read_) {
        return ZX_ERR_IO;
    }

    void* oob_src;
    zx_status_t status = GetPagePointers(byte_offset, nullptr, &oob_src);
    if (status != ZX_OK) {
        return status;
    }

    memcpy(bytes, oob_src, oob_size_);
    return ZX_OK;
}

zx_status_t FakeNandInterface::ReadPage(uint32_t byte_offset, void* bytes, uint32_t* actual) {
    if (fail_read_) {
        return ZX_ERR_IO;
    }

    void* data_src;
    zx_status_t status = GetPagePointers(byte_offset, &data_src, nullptr);
    if (status != ZX_OK) {
        return status;
    }

    memcpy(bytes, data_src, page_size_);
    *actual = read_actual_;
    return ZX_OK;
}

zx_status_t FakeNandInterface::WritePage(uint32_t byte_offset, const void* data, const void* oob) {
    if (fail_write_) {
        return ZX_ERR_IO;
    }

    void* data_dest;
    void* oob_dest;

    zx_status_t status = GetPagePointers(byte_offset, &data_dest, &oob_dest);
    if (status != ZX_OK) {
        return status;
    }

    memcpy(data_dest, data, page_size_);
    memcpy(oob_dest, oob, oob_size_);
    return ZX_OK;
}

zx_status_t FakeNandInterface::EraseBlock(uint32_t byte_offset) {
    if (fail_erase_ || byte_offset % block_size_ != 0) {
        return ZX_ERR_IO;
    }

    void* data_dest;
    void* oob_dest;

    zx_status_t status = GetPagePointers(byte_offset, &data_dest, &oob_dest);
    if (status != ZX_OK) {
        return status;
    }

    size_t data_size = block_size_ / page_size_ * (page_size_ + oob_size_);
    memset(data_dest, 0xFF, data_size);
    return ZX_OK;
}

zx_status_t FakeNandInterface::IsBadBlock(uint32_t byte_offset, bool* is_bad_block) {
    if (fail_is_bad_block_ || byte_offset % block_size_ != 0) {
        return ZX_ERR_IO;
    }

    uint32_t block_num = byte_offset / block_size_;
    *is_bad_block = bad_blocks_[block_num];
    return ZX_OK;
}

zx_status_t FakeNandInterface::GetPagePointers(uint32_t byte_offset, void** data, void** oob) {
    if (byte_offset % page_size_ != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t page_index = byte_offset / page_size_;
    uint32_t page_offset = page_index * (page_size_ + oob_size_);
    uint32_t oob_offset = page_offset + page_size_;

    if (data) {
        *data = &data_[page_offset];
    }

    if (oob) {
        *oob = &data_[oob_offset];
    }

    return ZX_OK;
}

void FakeNandInterface::SetBadBlock(uint32_t block_num, bool is_bad) {
    bad_blocks_[block_num] = is_bad;
}

} // namespace ftl_mtd
