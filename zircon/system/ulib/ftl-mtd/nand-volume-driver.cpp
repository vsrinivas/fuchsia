// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

#include <zircon/assert.h>

#include <lib/ftl-mtd/nand-volume-driver.h>

namespace ftl_mtd {

using namespace std::placeholders;

zx_status_t NandVolumeDriver::Create(uint32_t block_offset, uint32_t max_bad_blocks,
                                     std::unique_ptr<mtd::NandInterface> interface,
                                     std::unique_ptr<NandVolumeDriver>* out) {
    uint32_t page_multiplier = 1;
    while (page_multiplier * interface->OobSize() < kMinimumOobSize) {
        page_multiplier *= 2;
    }

    *out = std::unique_ptr<NandVolumeDriver>(
        new NandVolumeDriver(block_offset, max_bad_blocks, page_multiplier, std::move(interface)));
    return ZX_OK;
}

NandVolumeDriver::NandVolumeDriver(uint32_t block_offset, uint32_t max_bad_blocks,
                                   uint32_t page_multiplier,
                                   std::unique_ptr<mtd::NandInterface> interface)
    : block_offset_(block_offset), page_multiplier_(page_multiplier),
      max_bad_blocks_(max_bad_blocks), interface_(std::move(interface)) {}

const char* NandVolumeDriver::Init() {
    return nullptr;
}

const char* NandVolumeDriver::Attach(const ftl::Volume* ftl_volume) {
    ftl::VolumeOptions options = {
        .num_blocks = (interface_->Size() - ByteOffset()) / interface_->BlockSize(),
        // This should be 2%, but that is of the whole device, not just this partition.
        .max_bad_blocks = max_bad_blocks_,
        .block_size = interface_->BlockSize(),
        .page_size = MappedPageSize(),
        .eb_size = MappedOobSize(),
        .flags = 0 // Same as FSF_DRVR_PAGES (current default).
    };

    return CreateNdmVolume(ftl_volume, options);
}

bool NandVolumeDriver::Detach() {
    return RemoveNdmVolume();
}

int NandVolumeDriver::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                                const void* oob_buffer) {
    uint32_t real_start_page;
    uint32_t real_end_page;
    if (GetPageIndices(start_page, page_count, &real_start_page, &real_end_page) != ZX_OK) {
        return ftl::kNdmFatalError;
    }

    const uint8_t* page_buffer_ptr = reinterpret_cast<const uint8_t*>(page_buffer);
    const uint8_t* oob_buffer_ptr = reinterpret_cast<const uint8_t*>(oob_buffer);

    for (uint32_t page = real_start_page; page < real_end_page; page++) {
        zx_status_t status = interface_->WritePage(
            GetByteOffsetForPage(page), page_buffer_ptr, oob_buffer_ptr);

        // We checked the inputs before, so the interface should never return ZX_ERR_INVALID_ARGS.
        ZX_ASSERT(status != ZX_ERR_INVALID_ARGS);

        if (status != ZX_OK) {
            return ftl::kNdmError;
        }

        page_buffer_ptr += interface_->PageSize();
        oob_buffer_ptr += interface_->OobSize();
    }

    return ftl::kNdmOk;
}

int NandVolumeDriver::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                               void* oob_buffer) {
    uint32_t real_start_page;
    uint32_t real_end_page;
    if (GetPageIndices(start_page, page_count, &real_start_page, &real_end_page) != ZX_OK) {
        return ftl::kNdmFatalError;
    }

    uint8_t* page_buffer_ptr = reinterpret_cast<uint8_t*>(page_buffer);
    uint8_t* oob_buffer_ptr = reinterpret_cast<uint8_t*>(oob_buffer);

    for (uint32_t page = real_start_page; page < real_end_page; page++) {
        zx_status_t status = ReadPageAndOob(
            GetByteOffsetForPage(page), page_buffer_ptr, oob_buffer_ptr);

        if (status != ZX_OK) {
            return ftl::kNdmFatalError;
        }

        if (page_buffer) {
            page_buffer_ptr += interface_->PageSize();
        }

        if (oob_buffer) {
            oob_buffer_ptr += interface_->OobSize();
        }
    }

    return ftl::kNdmOk;
}

int NandVolumeDriver::NandErase(uint32_t page_num) {
    uint32_t real_start_page;
    uint32_t real_end_page;
    if (GetPageIndices(page_num, 1, &real_start_page, &real_end_page) != ZX_OK) {
        return ftl::kNdmError;
    }

    zx_status_t status = interface_->EraseBlock(GetBlockOffsetForPage(real_start_page));
    return status == ZX_OK ? ftl::kNdmOk : ftl::kNdmError;
}

int NandVolumeDriver::IsBadBlock(uint32_t page_num) {
    bool is_bad_block = false;

    uint32_t real_start_page;
    uint32_t real_end_page;
    if (GetPageIndices(page_num, 1, &real_start_page, &real_end_page) != ZX_OK) {
        return ftl::kNdmError;
    }

    zx_status_t status = interface_->IsBadBlock(GetBlockOffsetForPage(real_start_page),
                                                &is_bad_block);
    if (status != ZX_OK) {
        return ftl::kNdmError;
    }

    return is_bad_block ? ftl::kTrue : ftl::kFalse;
}

bool NandVolumeDriver::IsEmptyPage(uint32_t page_num, const uint8_t* page_buffer,
                                   const uint8_t* oob_buffer) {
    return IsEmptyPageImpl(page_buffer, MappedPageSize(), oob_buffer, MappedOobSize());
}

zx_status_t NandVolumeDriver::ReadPageAndOob(uint32_t byte_offset, void* page_buffer,
                                             void* oob_buffer) {
    if (page_buffer) {
        uint32_t actual;
        zx_status_t read_page_status = interface_->ReadPage(byte_offset, page_buffer, &actual);

        if (read_page_status != ZX_OK) {
            return read_page_status;
        }

        if (actual != interface_->PageSize()) {
            return ZX_ERR_IO_DATA_LOSS;
        }
    }

    if (oob_buffer) {
        zx_status_t read_oob_status = interface_->ReadOob(byte_offset, oob_buffer);
        if (read_oob_status != ZX_OK) {
            return read_oob_status;
        }
    }

    return ZX_OK;
}

zx_status_t NandVolumeDriver::GetPageIndices(uint32_t mapped_page, uint32_t mapped_page_count,
                                             uint32_t* start_page, uint32_t* end_page) {
    uint32_t start = ByteOffset() / interface_->PageSize() + page_multiplier_ * mapped_page;
    uint32_t end = start + page_multiplier_ * mapped_page_count;
    uint32_t last_page = interface_->Size() / interface_->PageSize();

    if (start >= last_page || end > last_page) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    *start_page = start;
    *end_page = end;
    return ZX_OK;
}

uint32_t NandVolumeDriver::GetBlockOffsetForPage(uint32_t real_page) {
    return GetByteOffsetForPage(real_page) / interface_->BlockSize() * interface_->BlockSize();
}

uint32_t NandVolumeDriver::GetByteOffsetForPage(uint32_t real_page) {
    return real_page * interface_->PageSize();
}

uint32_t NandVolumeDriver::ByteOffset() {
    return block_offset_ * interface_->BlockSize();
}

uint32_t NandVolumeDriver::MappedPageSize() {
    return page_multiplier_ * interface_->PageSize();
}

uint32_t NandVolumeDriver::MappedOobSize() {
    return page_multiplier_ * interface_->OobSize();
}

} // namespace ftl_mtd
