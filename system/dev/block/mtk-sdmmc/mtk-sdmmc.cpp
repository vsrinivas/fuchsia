// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-sdmmc.h"
#include "mtk-sdmmc-reg.h"

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-device-lib.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <hw/sdio.h>
#include <hw/sdmmc.h>

namespace {

constexpr uint32_t kPageMask = PAGE_SIZE - 1;
constexpr uint32_t kMsdcSrcCkFreq = 188000000;
constexpr uint32_t kIdentificationModeBusFreq = 400000;

}  // namespace

namespace sdmmc {

zx_status_t MtkSdmmc::Create(zx_device_t* parent) {
    zx_status_t status;

    pdev_protocol_t pdev;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev)) != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
        return status;
    }

    zx_handle_t bti_handle;
    if ((status = pdev_get_bti(&pdev, 0, &bti_handle)) != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_bti failed\n", __FILE__);
        return status;
    }

    zx::bti bti(bti_handle);

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed\n", __FILE__);
        return status;
    }

    ddk::MmioBuffer mmio_obj(mmio);

    uint32_t fifo_depth;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &fifo_depth, sizeof(fifo_depth),
                                 &actual);
    if (status != ZX_OK || actual != sizeof(fifo_depth)) {
        zxlogf(ERROR, "%s: DdkGetMetadata failed\n", __FILE__);
        return status;
    }

    sdmmc_host_info_t info = {
        .caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_AUTO_CMD12 | SDMMC_HOST_CAP_ADMA2,
        // TODO(bradenkell): Support descriptor DMA for reading/writing multiple pages.
        .max_transfer_size = PAGE_SIZE,
        .max_transfer_size_non_dma = fifo_depth,
        // TODO(bradenkell): Remove these once tuning is implemented.
        .prefs = SDMMC_HOST_PREFS_DISABLE_HS200 | SDMMC_HOST_PREFS_DISABLE_HS400
    };

    fbl::AllocChecker ac;
    fbl::unique_ptr<MtkSdmmc> device(new (&ac) MtkSdmmc(parent, fbl::move(mmio_obj), fbl::move(bti),
                                                        info));

    if (!ac.check()) {
        zxlogf(ERROR, "%s: MtkSdmmc alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    device->Init();

    if ((status = device->DdkAdd("mtk-sdmmc")) != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

void MtkSdmmc::Init() {
    // Set bus clock to f_OD (400 kHZ) for identification mode.
    SdmmcSetBusFreq(kIdentificationModeBusFreq);

    // Set read timeout to the maximum so SEND_EXT_CSD at f_OD succeeds.
    SdcCfg::Get()
        .ReadFrom(&mmio_)
        .set_bus_width(SdcCfg::kBusWidth1)
        .set_read_timeout(SdcCfg::kReadTimeoutMax)
        .WriteTo(&mmio_);
}

zx_status_t MtkSdmmc::SdmmcHostInfo(sdmmc_host_info_t* info) {
    memcpy(info, &info_, sizeof(info_));
    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
    // TODO(bradenkell): According to the schematic VCCQ is fixed at 1.8V. Verify this and update.
    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetBusWidth(sdmmc_bus_width_t bus_width) {
    uint32_t bus_width_value;

    switch (bus_width) {
    case SDMMC_BUS_WIDTH_MAX:
    case SDMMC_BUS_WIDTH_EIGHT:
        bus_width_value = SdcCfg::kBusWidth8;
        break;
    case SDMMC_BUS_WIDTH_FOUR:
        bus_width_value = SdcCfg::kBusWidth4;
        break;
    case SDMMC_BUS_WIDTH_ONE:
    default:
        bus_width_value = SdcCfg::kBusWidth1;
        break;
    }

    SdcCfg::Get().ReadFrom(&mmio_).set_bus_width(bus_width_value).WriteTo(&mmio_);

    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetBusFreq(uint32_t bus_freq) {
    if (bus_freq == 0) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // The bus clock frequency is determined as follows:
    // msdc_ck = cckdiv=0: msdc_src_ck / 2
    //           cckdiv>0: msdc_src_ck / (4 * cckdiv)
    // In DDR mode the bus clock is half of msdc_ck.

    // TODO(bradenkell): Double the requested frequency if DDR mode is currently selected.

    uint32_t cckdiv = kMsdcSrcCkFreq / bus_freq;
    cckdiv = ((cckdiv + 3) >> 2) & 0xff;  // Round the divider up, i.e. to a lower frequency.
    uint32_t actual = cckdiv == 0 ? kMsdcSrcCkFreq >> 1 : kMsdcSrcCkFreq / (cckdiv << 2);
    zxlogf(INFO, "MtkSdmmc::%s: requested frequency=%u, actual frequency=%u\n", __FUNCTION__,
           bus_freq, actual);

    MsdcCfg::Get().ReadFrom(&mmio_).set_card_ck_div(cckdiv).WriteTo(&mmio_);

    // TODO(bradenkell): Check stability with the ccksb bit.

    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
    uint32_t ck_mode;

    switch (timing) {
    case SDMMC_TIMING_HSDDR:
        ck_mode = MsdcCfg::kCardCkModeDdr;
        // TODO(bradenkell): Double msdc_ck to maintain the selected bus frequency.
        break;
    case SDMMC_TIMING_HS400:
    case SDMMC_TIMING_DDR50:
        return ZX_ERR_NOT_SUPPORTED;
    default:
        ck_mode = MsdcCfg::kCardCkModeDiv;
        break;
    }

    MsdcCfg::Get().ReadFrom(&mmio_).set_card_ck_mode(ck_mode).WriteTo(&mmio_);

    return ZX_OK;
}

void MtkSdmmc::SdmmcHwReset() {
    // TODO(bradenkell): Use MSDC0_RTSB (GPIO 114) to reset the eMMC chip.
}

zx_status_t MtkSdmmc::SdmmcPerformTuning(uint32_t cmd_idx) {
    // TODO(bradenkell): Implement this.
    return ZX_OK;
}

zx_status_t MtkSdmmc::RequestPrepareDma(sdmmc_req_t* req) {
    const uint64_t req_len = req->blockcount * req->blocksize;
    const bool is_read = req->cmd_flags & SDMMC_CMD_READ;

    // TODO(bradenkell): Support descriptor DMA for reading/writing multiple pages.

    zx_paddr_t phys;
    uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
    zx_status_t status = zx_bti_pin(bti_.get(), options, req->dma_vmo, req->buf_offset & ~kPageMask,
                                    PAGE_SIZE, &phys, 1, pmt_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to pin DMA buffer\n", __FILE__);
        return status;
    }

    if (is_read) {
        status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, req->buf_offset,
                                 req_len, NULL, 0);
    } else {
        status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN, req->buf_offset, req_len,
                                 NULL, 0);
    }

    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Cache invalidate failed\n", __FILE__);
        pmt_.unpin();
        return status;
    }

    MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(0).WriteTo(&mmio_);

    DmaLength::Get().FromValue(static_cast<uint32_t>(req_len)).WriteTo(&mmio_);
    DmaStartAddr::Get().FromValue(0).set(phys).WriteTo(&mmio_);
    DmaStartAddrHigh4Bits::Get().FromValue(0).set(phys).WriteTo(&mmio_);

    return ZX_OK;
}

zx_status_t MtkSdmmc::RequestFinishDma(sdmmc_req_t* req) {
    while (!MsdcInt::Get().ReadFrom(&mmio_).cmd_ready()) {}

    DmaCtrl::Get()
        .ReadFrom(&mmio_)
        .set_last_buffer(1)
        .set_dma_start(1)
        .WriteTo(&mmio_);

    while (!MsdcInt::Get().ReadFrom(&mmio_).transfer_complete()) {}

    DmaCtrl::Get().ReadFrom(&mmio_).set_dma_stop(1).WriteTo(&mmio_);
    while (DmaCfg::Get().ReadFrom(&mmio_).dma_active()) {}

    zx_status_t cache_status = ZX_OK;
    if (req->cmd_flags & SDMMC_CMD_READ) {
        const uint64_t req_len = req->blockcount * req->blocksize;
        cache_status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                       req->buf_offset, req_len, NULL, 0);
    }

    zx_status_t unpin_status = pmt_.unpin();

    if (cache_status != ZX_OK) {
        zxlogf(ERROR, "%s: Cache invalidate failed\n", __FILE__);
        return cache_status;
    } else if (unpin_status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to unpin DMA buffer\n", __FILE__);
        return unpin_status;
    }

    return ZX_OK;
}

zx_status_t MtkSdmmc::RequestPreparePolled(sdmmc_req_t* req) {
    MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(1).WriteTo(&mmio_);

    // Clear the FIFO.
    MsdcFifoCs::Get().ReadFrom(&mmio_).set_fifo_clear(1).WriteTo(&mmio_);
    while (MsdcFifoCs::Get().ReadFrom(&mmio_).fifo_clear()) {}

    return ZX_OK;
}

zx_status_t MtkSdmmc::RequestFinishPolled(sdmmc_req_t* req) {
    uint32_t bytes_remaining = req->blockcount * req->blocksize;
    uint8_t* data_ptr = reinterpret_cast<uint8_t*>(req->virt_buffer) + req->buf_offset;
    while (bytes_remaining > 0) {
        uint32_t fifo_count = MsdcFifoCs::Get().ReadFrom(&mmio_).rx_fifo_count();

        for (uint32_t i = 0; i < fifo_count; i++) {
            *data_ptr++ = MsdcRxData::Get().ReadFrom(&mmio_).data();
        }

        bytes_remaining -= fifo_count;
    }

    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcRequest(sdmmc_req_t* req) {
    uint32_t is_data_request = req->cmd_flags & SDMMC_RESP_DATA_PRESENT;
    if (is_data_request && !req->use_dma && !(req->cmd_flags & SDMMC_CMD_READ)) {
        // TODO(bradenkell): Implement polled block writes.
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    if (is_data_request) {
        status = req->use_dma ? RequestPrepareDma(req) : RequestPreparePolled(req);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: %s request prepare failed\n", __FILE__,
                   req->use_dma ? "DMA" : "PIO");
            return status;
        }
    }

    SdcBlockNum::Get().FromValue(req->blockcount < 1 ? 1 : req->blockcount).WriteTo(&mmio_);

    // Clear all interrupt bits.
    MsdcInt::Get().FromValue(MsdcInt::kAllInterruptBits).WriteTo(&mmio_);

    SdcArg::Get().FromValue(req->arg).WriteTo(&mmio_);
    SdcCmd::FromRequest(req).WriteTo(&mmio_);

    if (is_data_request) {
        status = req->use_dma ? RequestFinishDma(req) : RequestFinishPolled(req);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: %s response read failed\n", __FILE__, req->use_dma ? "DMA" : "PIO");
        }
    }

    while (SdcStatus::Get().ReadFrom(&mmio_).busy()) {}

    if (req->cmd_flags & SDMMC_RESP_LEN_136) {
        req->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
        req->response[1] = SdcResponse::Get(1).ReadFrom(&mmio_).response();
        req->response[2] = SdcResponse::Get(2).ReadFrom(&mmio_).response();
        req->response[3] = SdcResponse::Get(3).ReadFrom(&mmio_).response();
    } else if (req->cmd_flags & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
        req->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
    }

    MsdcInt int_reg = MsdcInt::Get().ReadFrom(&mmio_);
    if (int_reg.cmd_timeout() || int_reg.data_timeout()) {
        return ZX_ERR_TIMED_OUT;
    } else if (int_reg.cmd_crc_err() || int_reg.data_crc_err()) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    return ZX_OK;
}

}  // namespace sdmmc

extern "C" zx_status_t mtk_sdmmc_bind(void* ctx, zx_device_t* parent) {
    return sdmmc::MtkSdmmc::Create(parent);
}
