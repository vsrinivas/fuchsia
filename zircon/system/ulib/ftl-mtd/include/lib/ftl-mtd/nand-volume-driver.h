// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fbl/macros.h>
#include <lib/ftl/ndm-driver.h>
#include <zircon/types.h>

#include <lib/mtd/nand-interface.h>

namespace ftl_mtd {

constexpr uint32_t kMinimumOobSize = 16;

class NandVolumeDriver : public ftl::NdmBaseDriver {
public:
    // Creates an instance of NandVolumeDriver.
    // |max_bad_blocks| should be less than the number of blocks exposed by |interface|.
    static zx_status_t Create(uint32_t block_offset, uint32_t max_bad_blocks,
                              std::unique_ptr<mtd::NandInterface> interface,
                              std::unique_ptr<NandVolumeDriver>* out);

    virtual ~NandVolumeDriver() {}

    // NdmBaseDriver interface:
    const char* Init();
    const char* Attach(const ftl::Volume* ftl_volume);
    bool Detach();
    int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                  const void* oob_buffer);
    int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer);
    int NandErase(uint32_t page_num);
    int IsBadBlock(uint32_t page_num);
    bool IsEmptyPage(uint32_t page_num, const uint8_t* page_buffer, const uint8_t* oob_buffer);

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandVolumeDriver);

private:
    NandVolumeDriver(uint32_t byte_offset, uint32_t max_bad_blocks, uint32_t page_multiplier,
                     std::unique_ptr<mtd::NandInterface> interface);

    zx_status_t ReadPageAndOob(uint32_t byte_offset, void* page_buffer, void* oob_buffer);

    zx_status_t GetPageIndices(uint32_t mapped_page, uint32_t mapped_page_count,
                               uint32_t* start_page, uint32_t* end_page);
    uint32_t GetByteOffsetForPage(uint32_t real_page);

    uint32_t ByteOffset();
    uint32_t MappedPageSize();
    uint32_t MappedOobSize();

    uint32_t block_offset_;
    uint32_t page_multiplier_;
    uint32_t max_bad_blocks_;
    std::unique_ptr<mtd::NandInterface> interface_;
};

} // namespace ftl_mtd
