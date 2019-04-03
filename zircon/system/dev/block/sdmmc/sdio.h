// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/sdio.h>
#include <ddktl/device.h>
#include <ddktl/protocol/sdio.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>

#include "sdmmc.h"

namespace sdmmc {

class Sdio;
using DeviceType = ddk::Device<Sdio>;

class Sdio : public DeviceType, public ddk::SdioProtocol<Sdio, ddk::base_protocol> {
public:
    static void Create(zx_device_t* parent, sdmmc_device* sdmmc_dev,
                       fbl::unique_ptr<Sdio>* out_dev);

    void DdkRelease() {}

    zx_status_t SdioProbe();

    zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info);
    zx_status_t SdioEnableFn(uint8_t fn_idx);
    zx_status_t SdioDisableFn(uint8_t fn_idx);
    zx_status_t SdioEnableFnIntr(uint8_t fn_idx);
    zx_status_t SdioDisableFnIntr(uint8_t fn_idx);
    zx_status_t SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt);
    zx_status_t SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size);
    zx_status_t SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn);
    zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                             uint8_t* out_read_byte);
    zx_status_t SdioGetInBandIntr(zx::interrupt* out_irq);

private:
    struct SdioFuncTuple {
        uint8_t tuple_code;
        uint8_t tuple_body_size;
        uint8_t tuple_body[UINT8_MAX];
    };

    Sdio(zx_device_t* parent, struct sdmmc_device* sdmmc_dev)
        : DeviceType(parent), sdmmc_dev_(sdmmc_dev) {}

    zx_status_t SdioReset();
    // Reads the card common control registers (CCCR) to enumerate the card's capabilities.
    zx_status_t ProcessCccr();
    // Reads the card information structure (CIS) for the given function to get the manufacturer
    // identification and function extensions tuples.
    zx_status_t ProcessCis(uint32_t fn_idx);
    // Parses a tuple read from the CIS.
    zx_status_t ParseFnTuple(uint32_t fn_idx, SdioFuncTuple* tup);
    // Parses the manufacturer ID tuple and saves it in the given function's struct.
    zx_status_t ParseMfidTuple(uint32_t fn_idx, SdioFuncTuple* tup);
    // Parses the function extensions tuple and saves it in the given function's struct.
    zx_status_t ParseFuncExtTuple(uint32_t fn_idx, SdioFuncTuple* tup);
    // Reads the I/O function code and saves it in the given function's struct.
    zx_status_t ProcessFbr(uint8_t fn_idx);
    // Popluates the given function's struct by calling the methods above. Also enables the
    // function and sets its default block size.
    zx_status_t InitFunc(uint8_t fn_idx);

    zx_status_t SwitchFreq(uint32_t new_freq);
    zx_status_t TrySwitchHs();
    zx_status_t TrySwitchUhs();
    zx_status_t Enable4BitBus();
    zx_status_t SwitchBusWidth(uint32_t bw);

    zx_status_t ReadData16(uint8_t fn_idx, uint32_t addr, uint16_t* word);
    zx_status_t WriteData16(uint8_t fn_idx, uint32_t addr, uint16_t word);

    sdmmc_device* sdmmc_dev_;
};

}  // namespace sdmmc
