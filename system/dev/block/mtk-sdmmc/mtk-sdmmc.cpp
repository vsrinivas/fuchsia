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
#include <lib/fzl/vmo-mapper.h>

namespace {

constexpr uint32_t kPageMask = PAGE_SIZE - 1;
constexpr uint32_t kMsdcSrcCkFreq = 188000000;
constexpr uint32_t kIdentificationModeBusFreq = 400000;
constexpr int kTuningDelayIterations = 4;

constexpr uint8_t kTuningBlockPattern4Bit[64] = {
    0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
    0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
    0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
    0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
    0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
    0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
    0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
    0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

constexpr uint8_t kTuningBlockPattern8Bit[128] = {
    0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
    0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
    0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
    0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
    0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
    0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
    0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
    0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
    0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
    0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
    0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
    0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
    0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
    0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

// Returns false if all tuning tests failed. Chooses the best window and sets sample and delay to
// the optimal sample edge and delay values.
bool GetBestWindow(const sdmmc::TuneWindow& rising_window, const sdmmc::TuneWindow& falling_window,
                   uint32_t* sample, uint32_t* delay) {
    uint32_t rising_value = 0;
    uint32_t falling_value = 0;
    uint32_t rising_size = rising_window.GetDelay(&rising_value);
    uint32_t falling_size = falling_window.GetDelay(&falling_value);

    if (rising_size == 0 && falling_size == 0) {
        return false;
    }

    if (falling_size > rising_size) {
        *sample = sdmmc::MsdcIoCon::kSampleFallingEdge;
        *delay = falling_value;
    } else {
        *sample = sdmmc::MsdcIoCon::kSampleRisingEdge;
        *delay = rising_value;
    }

    return true;
}

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
        // TODO(bradenkell): Remove this once HS400 has been tested.
        .prefs = SDMMC_HOST_PREFS_DISABLE_HS400
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

    SdcCfg::Get().ReadFrom(&mmio_).set_bus_width(SdcCfg::kBusWidth1).WriteTo(&mmio_);
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

    // For kCardCkModeDiv the bus clock frequency is determined as follows:
    //     msdc_ck = card_ck_div=0: msdc_src_ck / 2
    //               card_ck_div>0: msdc_src_ck / (4 * card_ck_div)
    // For kCardCkModeNoDiv the bus clock frequency is msdc_src_ck
    // For kCardCkModeDdr the bus clock frequency half that of kCardCkModeDiv.
    // For kCardCkModeHs400 the bus clock frequency is the same as kCardCkModeDiv, unless
    // hs400_ck_mode is set in which case it is the same as kCardCkModeNoDiv.

    auto msdc_cfg = MsdcCfg::Get().ReadFrom(&mmio_);

    uint32_t ck_mode = msdc_cfg.card_ck_mode();
    const bool is_ddr = (ck_mode == MsdcCfg::kCardCkModeDdr ||
                         ck_mode == MsdcCfg::kCardCkModeHs400);

    uint32_t hs400_ck_mode = msdc_cfg.hs400_ck_mode();

    // Double the requested frequency if a DDR mode is currently selected.
    uint32_t requested = is_ddr ? bus_freq * 2 : bus_freq;

    // Round the divider up, i.e. to a lower frequency.
    uint32_t ck_div = (((kMsdcSrcCkFreq / requested) + 3) / 4);
    if (requested >= kMsdcSrcCkFreq / 2) {
        ck_div = 0;
    } else if (ck_div > 0xff) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    msdc_cfg.set_ck_pwr_down(0).WriteTo(&mmio_);

    if (ck_mode == MsdcCfg::kCardCkModeHs400) {
        hs400_ck_mode = requested >= kMsdcSrcCkFreq ? 1 : 0;
    } else if (!is_ddr) {
        ck_mode = requested >= kMsdcSrcCkFreq ? MsdcCfg::kCardCkModeNoDiv : MsdcCfg::kCardCkModeDiv;
    }

    msdc_cfg.set_hs400_ck_mode(hs400_ck_mode)
        .set_card_ck_mode(ck_mode)
        .set_card_ck_div(ck_div)
        .WriteTo(&mmio_);

    while (!msdc_cfg.ReadFrom(&mmio_).card_ck_stable()) {}
    msdc_cfg.set_ck_pwr_down(1).WriteTo(&mmio_);

    return ZX_OK;
}

zx_status_t MtkSdmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
    uint32_t ck_mode;

    MsdcCfg::Get().ReadFrom(&mmio_).set_ck_pwr_down(0).WriteTo(&mmio_);

    switch (timing) {
    case SDMMC_TIMING_DDR50:
    case SDMMC_TIMING_HSDDR:
        ck_mode = MsdcCfg::kCardCkModeDdr;
        break;
    case SDMMC_TIMING_HS400:
        ck_mode = MsdcCfg::kCardCkModeHs400;
        break;
    default:
        ck_mode = MsdcCfg::kCardCkModeDiv;
        break;
    }

    MsdcCfg::Get().ReadFrom(&mmio_).set_card_ck_mode(ck_mode).WriteTo(&mmio_);
    while (!MsdcCfg::Get().ReadFrom(&mmio_).card_ck_stable()) {}
    MsdcCfg::Get().ReadFrom(&mmio_).set_ck_pwr_down(1).WriteTo(&mmio_);

    return ZX_OK;
}

void MtkSdmmc::SdmmcHwReset() {
    // TODO(bradenkell): Use MSDC0_RTSB (GPIO 114) to reset the eMMC chip.
    MsdcCfg::Get().ReadFrom(&mmio_).set_reset(1).WriteTo(&mmio_);
    while (MsdcCfg::Get().ReadFrom(&mmio_).reset()) {}
}

RequestStatus MtkSdmmc::SendTuningBlock(uint32_t cmd_idx, zx_handle_t vmo) {
    uint32_t bus_width = SdcCfg::Get().ReadFrom(&mmio_).bus_width();

    sdmmc_req_t request;
    request.cmd_idx = cmd_idx;
    request.cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS;
    request.arg = 0;
    request.blockcount = 1;
    request.blocksize = bus_width == SdcCfg::kBusWidth4 ? sizeof(kTuningBlockPattern4Bit)
                                                        : sizeof(kTuningBlockPattern8Bit);
    request.use_dma = true;
    request.dma_vmo = vmo;
    request.buf_offset = 0;

    RequestStatus status = SdmmcRequestWithStatus(&request);
    if (status.Get() != ZX_OK) {
        return status;
    }

    const uint8_t* tuning_block_pattern = kTuningBlockPattern8Bit;
    if (bus_width == SdcCfg::kBusWidth4) {
        tuning_block_pattern = kTuningBlockPattern4Bit;
    }

    uint8_t buf[sizeof(kTuningBlockPattern8Bit)];
    if ((status.data_status = zx_vmo_read(vmo, buf, 0, request.blocksize)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to read VMO\n", __FILE__);
        return status;
    }

    status.data_status = memcmp(buf, tuning_block_pattern, request.blocksize) == 0 ? ZX_OK
                                                                                   : ZX_ERR_IO;
    return status;
}

template <typename DelayCallback, typename RequestCallback>
void MtkSdmmc::TestDelaySettings(DelayCallback&& set_delay, RequestCallback&& do_request,
                                 TuneWindow* window) {
    for (uint32_t delay = 0; delay <= PadTune0::kDelayMax; delay++) {
        fbl::forward<DelayCallback>(set_delay)(delay);

        for (int i = 0; i < kTuningDelayIterations; i++) {
            if (fbl::forward<RequestCallback>(do_request)() != ZX_OK) {
                window->Fail();
                break;
            } else if (i == kTuningDelayIterations - 1) {
                window->Pass();
            }
        }
    }
}

zx_status_t MtkSdmmc::SdmmcPerformTuning(uint32_t cmd_idx) {
    uint32_t bus_width = SdcCfg::Get().ReadFrom(&mmio_).bus_width();
    if (bus_width != SdcCfg::kBusWidth4 && bus_width != SdcCfg::kBusWidth8) {
        return ZX_ERR_INTERNAL;
    }

    // Enable the cmd and data delay lines.
    auto pad_tune0 = PadTune0::Get()
                         .ReadFrom(&mmio_)
                         .set_cmd_delay_sel(1)
                         .set_data_delay_sel(1)
                         .WriteTo(&mmio_);

    auto msdc_iocon = MsdcIoCon::Get().ReadFrom(&mmio_);

    zx::vmo vmo;
    fzl::VmoMapper vmo_mapper;
    zx_status_t status = vmo_mapper.CreateAndMap(sizeof(kTuningBlockPattern8Bit),
                                                 ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                                 nullptr, &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to create and map VMO\n", __FILE__);
        return status;
    }

    auto set_cmd_delay = [this](uint32_t delay) {
        PadTune0::Get().ReadFrom(&mmio_).set_cmd_delay(delay).WriteTo(&mmio_);
    };

    zx_handle_t vmo_handle = vmo.get();
    auto test_cmd = [this, vmo_handle, cmd_idx]() {
        return SendTuningBlock(cmd_idx, vmo_handle).cmd_status;
    };

    TuneWindow cmd_rising_window, cmd_falling_window;

    // Find the best window when sampling on the clock rising edge.
    msdc_iocon.set_cmd_sample(MsdcIoCon::kSampleRisingEdge).WriteTo(&mmio_);
    TestDelaySettings(set_cmd_delay, test_cmd, &cmd_rising_window);

    // Find the best window when sampling on the clock falling edge.
    msdc_iocon.set_cmd_sample(MsdcIoCon::kSampleFallingEdge).WriteTo(&mmio_);
    TestDelaySettings(set_cmd_delay, test_cmd, &cmd_falling_window);

    uint32_t sample, delay;
    if (!GetBestWindow(cmd_rising_window, cmd_falling_window, &sample, &delay)) {
        return ZX_ERR_IO;
    }

    // Select the best sampling edge and delay value.
    msdc_iocon.set_cmd_sample(sample).WriteTo(&mmio_);
    pad_tune0.set_cmd_delay(delay).WriteTo(&mmio_);

    auto set_data_delay = [this](uint32_t delay) {
        PadTune0::Get().ReadFrom(&mmio_).set_data_delay(delay).WriteTo(&mmio_);
    };

    auto test_data = [this, vmo_handle, cmd_idx]() {
        return SendTuningBlock(cmd_idx, vmo_handle).Get();
    };

    // Repeat this process for the data bus.
    TuneWindow data_rising_window, data_falling_window;

    msdc_iocon.set_data_sample(MsdcIoCon::kSampleRisingEdge).WriteTo(&mmio_);
    TestDelaySettings(set_data_delay, test_data, &data_rising_window);

    msdc_iocon.set_data_sample(MsdcIoCon::kSampleFallingEdge).WriteTo(&mmio_);
    TestDelaySettings(set_data_delay, test_data, &data_falling_window);

    if (!GetBestWindow(data_rising_window, data_falling_window, &sample, &delay)) {
        return ZX_ERR_IO;
    }

    msdc_iocon.set_data_sample(sample).WriteTo(&mmio_);
    pad_tune0.set_data_delay(delay).WriteTo(&mmio_);

    return ZX_OK;
}

RequestStatus MtkSdmmc::RequestPrepareDma(sdmmc_req_t* req) {
    const uint64_t req_len = req->blockcount * req->blocksize;
    const bool is_read = req->cmd_flags & SDMMC_CMD_READ;

    // TODO(bradenkell): Support descriptor DMA for reading/writing multiple pages.

    RequestStatus status;

    zx_paddr_t phys;
    uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
    status.cmd_status = zx_bti_pin(bti_.get(), options, req->dma_vmo, req->buf_offset & ~kPageMask,
                                   PAGE_SIZE, &phys, 1, pmt_.reset_and_get_address());
    if (status.Get() != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to pin DMA buffer\n", __FILE__);
        return status;
    }

    if (is_read) {
        status.cmd_status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                            req->buf_offset, req_len, nullptr, 0);
    } else {
        status.cmd_status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN, req->buf_offset,
                                            req_len, nullptr, 0);
    }

    if (status.Get() != ZX_OK) {
        zxlogf(ERROR, "%s: Cache invalidate failed\n", __FILE__);
        pmt_.unpin();
    } else {
        MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(0).WriteTo(&mmio_);

        DmaLength::Get().FromValue(static_cast<uint32_t>(req_len)).WriteTo(&mmio_);
        DmaStartAddr::Get().FromValue(0).set(phys).WriteTo(&mmio_);
        DmaStartAddrHigh4Bits::Get().FromValue(0).set(phys).WriteTo(&mmio_);
    }

    return status;
}

RequestStatus MtkSdmmc::RequestFinishDma(sdmmc_req_t* req) {
    auto msdc_int = MsdcInt::Get().ReadFrom(&mmio_);

    while (!msdc_int.ReadFrom(&mmio_).CmdInterrupt()) {}
    if (msdc_int.cmd_crc_err()) {
        return RequestStatus(ZX_ERR_IO_DATA_INTEGRITY);
    } else if (msdc_int.cmd_timeout()) {
        return RequestStatus(ZX_ERR_TIMED_OUT);
    }

    DmaCtrl::Get()
        .ReadFrom(&mmio_)
        .set_last_buffer(1)
        .set_dma_start(1)
        .WriteTo(&mmio_);

    while (!msdc_int.ReadFrom(&mmio_).DataInterrupt()) {}

    RequestStatus status;
    if (msdc_int.data_crc_err()) {
        status.data_status = ZX_ERR_IO_DATA_INTEGRITY;
    } else if (msdc_int.data_timeout()) {
        status.data_status = ZX_ERR_TIMED_OUT;
    }

    DmaCtrl::Get().ReadFrom(&mmio_).set_dma_stop(1).WriteTo(&mmio_);
    while (DmaCfg::Get().ReadFrom(&mmio_).dma_active()) {}

    if (status.Get() != ZX_OK) {
        return status;
    }

    zx_status_t cache_status = ZX_OK;
    if (req->cmd_flags & SDMMC_CMD_READ) {
        const uint64_t req_len = req->blockcount * req->blocksize;
        cache_status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                       req->buf_offset, req_len, nullptr, 0);
    }

    zx_status_t unpin_status = pmt_.unpin();

    if (cache_status != ZX_OK) {
        zxlogf(ERROR, "%s: Cache invalidate failed\n", __FILE__);
        status.data_status = cache_status;
    } else if (unpin_status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to unpin DMA buffer\n", __FILE__);
        status.data_status = unpin_status;
    }

    return status;
}

RequestStatus MtkSdmmc::RequestPreparePolled(sdmmc_req_t* req) {
    MsdcCfg::Get().ReadFrom(&mmio_).set_pio_mode(1).WriteTo(&mmio_);

    // Clear the FIFO.
    MsdcFifoCs::Get().ReadFrom(&mmio_).set_fifo_clear(1).WriteTo(&mmio_);
    while (MsdcFifoCs::Get().ReadFrom(&mmio_).fifo_clear()) {}

    return RequestStatus();
}

RequestStatus MtkSdmmc::RequestFinishPolled(sdmmc_req_t* req) {
    while (SdcStatus::Get().ReadFrom(&mmio_).cmd_busy()) {}
    if (MsdcInt::Get().ReadFrom(&mmio_).cmd_crc_err()) {
        return RequestStatus(ZX_ERR_IO_DATA_INTEGRITY);
    } else if (MsdcInt::Get().ReadFrom(&mmio_).cmd_timeout()) {
        return RequestStatus(ZX_ERR_TIMED_OUT);
    }

    uint32_t bytes_remaining = req->blockcount * req->blocksize;
    uint8_t* data_ptr = reinterpret_cast<uint8_t*>(req->virt_buffer) + req->buf_offset;
    while (bytes_remaining > 0) {
        uint32_t fifo_count = MsdcFifoCs::Get().ReadFrom(&mmio_).rx_fifo_count();

        for (uint32_t i = 0; i < fifo_count; i++) {
            *data_ptr++ = MsdcRxData::Get().ReadFrom(&mmio_).data();
        }

        bytes_remaining -= fifo_count;
    }

    return RequestStatus();
}

zx_status_t MtkSdmmc::SdmmcRequest(sdmmc_req_t* req) {
    return SdmmcRequestWithStatus(req).Get();
}

RequestStatus MtkSdmmc::SdmmcRequestWithStatus(sdmmc_req_t* req) {
    uint32_t is_data_request = req->cmd_flags & SDMMC_RESP_DATA_PRESENT;
    if (is_data_request && !req->use_dma && !(req->cmd_flags & SDMMC_CMD_READ)) {
        // TODO(bradenkell): Implement polled block writes.
        return RequestStatus(ZX_ERR_NOT_SUPPORTED);
    }

    RequestStatus status;
    if (is_data_request) {
        status = req->use_dma ? RequestPrepareDma(req) : RequestPreparePolled(req);
        if (status.Get() != ZX_OK) {
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
    } else {
        while (SdcStatus::Get().ReadFrom(&mmio_).cmd_busy()) {}
        if (MsdcInt::Get().ReadFrom(&mmio_).cmd_crc_err()) {
            status.cmd_status = ZX_ERR_IO_DATA_INTEGRITY;
        } else if (MsdcInt::Get().ReadFrom(&mmio_).cmd_timeout()) {
            status.cmd_status = ZX_ERR_TIMED_OUT;
        }
    }

    if (status.Get() == ZX_OK) {
        if (req->cmd_flags & SDMMC_RESP_LEN_136) {
            req->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
            req->response[1] = SdcResponse::Get(1).ReadFrom(&mmio_).response();
            req->response[2] = SdcResponse::Get(2).ReadFrom(&mmio_).response();
            req->response[3] = SdcResponse::Get(3).ReadFrom(&mmio_).response();
        } else if (req->cmd_flags & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
            req->response[0] = SdcResponse::Get(0).ReadFrom(&mmio_).response();
        }
    } else {
        MsdcCfg::Get().ReadFrom(&mmio_).set_reset(1).WriteTo(&mmio_);
        while (MsdcCfg::Get().ReadFrom(&mmio_).reset()) {}
    }

    return status;
}

}  // namespace sdmmc

extern "C" zx_status_t mtk_sdmmc_bind(void* ctx, zx_device_t* parent) {
    return sdmmc::MtkSdmmc::Create(parent);
}
