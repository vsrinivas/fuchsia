// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "sdmmc-device.h"

#include <lib/mock-function/mock-function.h>

namespace sdmmc {

class MockSdmmcDevice : public SdmmcDevice {
public:
    MockSdmmcDevice(const sdmmc_host_info_t& host_info)
        : SdmmcDevice(ddk::SdmmcProtocolClient(), host_info) {}

    zx_status_t SdioIoRwExtended(uint32_t caps, bool write, uint32_t fn_idx, uint32_t reg_addr,
                                 bool incr, uint32_t blk_count, uint32_t blk_size, bool use_dma,
                                 uint8_t* buf, zx_handle_t dma_vmo, uint64_t buf_offset) override {
        return mock_sdio_io_rw_extended_.Call(caps, write, fn_idx, reg_addr, incr, blk_count,
                                              blk_size, buf_offset);
    }

    auto& mock_SdioIoRwExtended() { return mock_sdio_io_rw_extended_; }

    void VerifyAll() { mock_sdio_io_rw_extended_.VerifyAndClear(); }

private:
    mock_function::MockFunction<zx_status_t, uint32_t, bool, uint32_t, uint32_t, bool, uint32_t,
                                uint32_t, uint64_t>
        mock_sdio_io_rw_extended_;
};

}  // namespace sdmmc
