// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include "fake-nand-interface.h"

namespace ftl_mtd {

FakeNandInterface::FakeNandInterface(uint32_t page_size, uint32_t oob_size, uint32_t block_size,
                                     uint32_t size)
    : read_actual_(page_size), read_fails_(false), write_fails_(false), page_size_(page_size),
      oob_size_(oob_size), block_size_(block_size), size_(size) {
    size_t data_size = size_ / page_size_ * (page_size_ + oob_size_);
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
    if (read_fails_) {
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
    if (read_fails_) {
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
    if (write_fails_) {
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
    return ZX_OK;
}

zx_status_t FakeNandInterface::IsBadBlock(uint32_t byte_offset, bool* is_bad_block) {
    *is_bad_block = false;
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

} // namespace ftl_mtd
