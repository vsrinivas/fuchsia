// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/metadata.h>
#include <ddk/phys-iter.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/sdmmc.h>
#include <ddktl/pdev.h>

#include <hw/reg.h>
#include <hw/sdmmc.h>
#include <lib/sync/completion.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>

#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include "aml-sd-emmc.h"

// Limit maximum number of descriptors to 512 for now
#define AML_DMA_DESC_MAX_COUNT 512
#define AML_SD_EMMC_TRACE(fmt, ...) zxlogf(TRACE, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_INFO(fmt, ...) zxlogf(INFO, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_ERROR(fmt, ...) zxlogf(ERROR, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_COMMAND(c) ((0x80) | (c))
#define PAGE_MASK (PAGE_SIZE - 1ull)

static inline uint8_t log2_ceil(uint16_t blk_sz) {
    if (blk_sz == 1) {
        return 0;
    }
    uint8_t clz = static_cast<uint8_t>(__builtin_clz(static_cast<uint16_t>(blk_sz - 1)));
    return static_cast<uint8_t>(16 - clz);
}

namespace sdmmc {

void AmlSdEmmc::DumpRegs() const {
    AML_SD_EMMC_TRACE("sd_emmc_clock : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET));
    DumpSdmmcClock(mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_delay1 : 0x%x\n", mmio_.Read32(AML_SD_EMMC_DELAY1_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_delay2 : 0x%x\n", mmio_.Read32(AML_SD_EMMC_DELAY2_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_adjust : 0x%x\n", mmio_.Read32(AML_SD_EMMC_ADJUST_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_calout : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CALOUT_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_start : 0x%x\n", mmio_.Read32(AML_SD_EMMC_START_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cfg : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CFG_OFFSET));
    DumpSdmmcCfg(mmio_.Read32(AML_SD_EMMC_CFG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_status : 0x%x\n", mmio_.Read32(AML_SD_EMMC_STATUS_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_irq_en : 0x%x\n", mmio_.Read32(AML_SD_EMMC_IRQ_EN_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_cfg : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_CFG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_arg : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_ARG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_dat : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_DAT_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_RSP_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp1 : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_RSP1_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp2 : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_RSP2_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp3 : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_RSP3_OFFSET));
    AML_SD_EMMC_TRACE("bus_err : 0x%x\n", mmio_.Read32(AML_SD_EMMC_CMD_BUS_ERR_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_curr_cfg: 0x%x\n", mmio_.Read32(AML_SD_EMMC_CURR_CFG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_curr_arg: 0x%x\n", mmio_.Read32(AML_SD_EMMC_CURR_ARG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_curr_dat: 0x%x\n", mmio_.Read32(AML_SD_EMMC_CURR_DAT_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_curr_rsp: 0x%x\n", mmio_.Read32(AML_SD_EMMC_CURR_RSP_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_next_cfg: 0x%x\n", mmio_.Read32(AML_SD_EMMC_NXT_CFG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_next_arg: 0x%x\n", mmio_.Read32(AML_SD_EMMC_NXT_ARG_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_next_dat: 0x%x\n", mmio_.Read32(AML_SD_EMMC_NXT_DAT_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_next_rsp: 0x%x\n", mmio_.Read32(AML_SD_EMMC_NXT_RSP_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_rxd : 0x%x\n", mmio_.Read32(AML_SD_EMMC_RXD_OFFSET));
    AML_SD_EMMC_TRACE("sd_emmc_txd : 0x%x\n", mmio_.Read32(AML_SD_EMMC_TXD_OFFSET));
}

void AmlSdEmmc::DumpSdmmcStatus(uint32_t status) const {
    uint32_t rxd_err = get_bits(status, AML_SD_EMMC_STATUS_RXD_ERR_MASK,
                                AML_SD_EMMC_STATUS_RXD_ERR_LOC);
    AML_SD_EMMC_TRACE("Dumping sd_emmc_status 0x%0x\n", status);
    AML_SD_EMMC_TRACE("    RXD_ERR: %d\n", rxd_err);
    AML_SD_EMMC_TRACE("    TXD_ERR: %d\n", get_bit(status, AML_SD_EMMC_STATUS_TXD_ERR));
    AML_SD_EMMC_TRACE("    DESC_ERR: %d\n", get_bit(status, AML_SD_EMMC_STATUS_DESC_ERR));
    AML_SD_EMMC_TRACE("    RESP_ERR: %d\n", get_bit(status, AML_SD_EMMC_STATUS_RESP_ERR));
    AML_SD_EMMC_TRACE("    RESP_TIMEOUT: %d\n", get_bit(status, AML_SD_EMMC_STATUS_RESP_TIMEOUT));
    AML_SD_EMMC_TRACE("    DESC_TIMEOUT: %d\n", get_bit(status, AML_SD_EMMC_STATUS_DESC_TIMEOUT));
    AML_SD_EMMC_TRACE("    END_OF_CHAIN: %d\n", get_bit(status, AML_SD_EMMC_STATUS_END_OF_CHAIN));
    AML_SD_EMMC_TRACE("    DESC_IRQ: %d\n", get_bit(status, AML_SD_EMMC_STATUS_RESP_STATUS));
    AML_SD_EMMC_TRACE("    IRQ_SDIO: %d\n", get_bit(status, AML_SD_EMMC_STATUS_IRQ_SDIO));
    AML_SD_EMMC_TRACE("    DAT_I: %d\n", get_bits(status, AML_SD_EMMC_STATUS_DAT_I_MASK,
                                                  AML_SD_EMMC_STATUS_DAT_I_LOC));
    AML_SD_EMMC_TRACE("    CMD_I: %d\n", get_bit(status, AML_SD_EMMC_STATUS_CMD_I));
    AML_SD_EMMC_TRACE("    DS: %d\n", get_bit(status, AML_SD_EMMC_STATUS_DS));
    AML_SD_EMMC_TRACE("    BUS_FSM: %d\n", get_bits(status, AML_SD_EMMC_STATUS_BUS_FSM_MASK,
                                                    AML_SD_EMMC_STATUS_BUS_FSM_LOC));
    AML_SD_EMMC_TRACE("    BUS_DESC_BUSY: %d\n", get_bit(status, AML_SD_EMMC_STATUS_BUS_DESC_BUSY));
    AML_SD_EMMC_TRACE("    CORE_RDY: %d\n", get_bit(status, AML_SD_EMMC_STATUS_BUS_CORE_BUSY));
}

void AmlSdEmmc::DumpSdmmcCfg(uint32_t config) const {
    AML_SD_EMMC_TRACE("Dumping sd_emmc_cfg 0x%0x\n", config);
    AML_SD_EMMC_TRACE("    BUS_WIDTH: %d\n", get_bits(config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK,
                                                      AML_SD_EMMC_CFG_BUS_WIDTH_LOC));
    AML_SD_EMMC_TRACE("    DDR: %d\n", get_bit(config, AML_SD_EMMC_CFG_DDR));
    AML_SD_EMMC_TRACE("    DC_UGT: %d\n", get_bit(config, AML_SD_EMMC_CFG_DC_UGT));
    AML_SD_EMMC_TRACE("    BLOCK LEN: %d\n", get_bits(config, AML_SD_EMMC_CFG_BL_LEN_MASK,
                                                      AML_SD_EMMC_CFG_BL_LEN_LOC));
}

void AmlSdEmmc::DumpSdmmcClock(uint32_t clock) const {
    AML_SD_EMMC_TRACE("Dumping clock 0x%0x\n", clock);
    AML_SD_EMMC_TRACE("   DIV: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_DIV_MASK,
                                               AML_SD_EMMC_CLOCK_CFG_DIV_LOC));
    AML_SD_EMMC_TRACE("   SRC: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_SRC_MASK,
                                               AML_SD_EMMC_CLOCK_CFG_SRC_LOC));
    AML_SD_EMMC_TRACE("   CORE_PHASE: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK,
                                                      AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC));
    AML_SD_EMMC_TRACE("   TX_PHASE: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_TX_PHASE_MASK,
                                                    AML_SD_EMMC_CLOCK_CFG_TX_PHASE_LOC));
    AML_SD_EMMC_TRACE("   RX_PHASE: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_RX_PHASE_MASK,
                                                    AML_SD_EMMC_CLOCK_CFG_RX_PHASE_LOC));
    AML_SD_EMMC_TRACE("   TX_DELAY: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_TX_DELAY_MASK,
                                                    AML_SD_EMMC_CLOCK_CFG_TX_DELAY_LOC));
    AML_SD_EMMC_TRACE("   RX_DELAY: %d\n", get_bits(clock, AML_SD_EMMC_CLOCK_CFG_RX_DELAY_MASK,
                                                    AML_SD_EMMC_CLOCK_CFG_RX_DELAY_LOC));
    AML_SD_EMMC_TRACE("   ALWAYS_ON: %d\n", get_bit(clock, AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON));
}

void AmlSdEmmc::DumpSdmmcCmdCfg(uint32_t cmd_desc) const {
    AML_SD_EMMC_TRACE("Dumping cmd_cfg 0x%0x\n", cmd_desc);
    AML_SD_EMMC_TRACE("   REQ_LEN: %d\n", get_bits(cmd_desc, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                                                   AML_SD_EMMC_CMD_INFO_LEN_LOC));
    AML_SD_EMMC_TRACE("   BLOCK_MODE: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_BLOCK_MODE));
    AML_SD_EMMC_TRACE("   R1B: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_R1B));
    AML_SD_EMMC_TRACE("   END_OF_CHAIN: %d\n", get_bit(cmd_desc,
                                                       AML_SD_EMMC_CMD_INFO_END_OF_CHAIN));
    AML_SD_EMMC_TRACE("   TIMEOUT: %d\n", get_bits(cmd_desc, AML_SD_EMMC_CMD_INFO_TIMEOUT_MASK,
                                                   AML_SD_EMMC_CMD_INFO_TIMEOUT_LOC));
    AML_SD_EMMC_TRACE("   NO_RESP: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_NO_RESP));
    AML_SD_EMMC_TRACE("   NO_CMD: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_NO_CMD));
    AML_SD_EMMC_TRACE("   DATA_IO: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_DATA_IO));
    AML_SD_EMMC_TRACE("   DATA_WR: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_DATA_WR));
    AML_SD_EMMC_TRACE("   RESP_NO_CRC: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_RESP_NO_CRC));
    AML_SD_EMMC_TRACE("   RESP_128: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_RESP_128));
    AML_SD_EMMC_TRACE("   RESP_NUM: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_RESP_NUM));
    AML_SD_EMMC_TRACE("   DATA_NUM: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_DATA_NUM));
    AML_SD_EMMC_TRACE("   CMD_IDX: %d\n", get_bits(cmd_desc, AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK,
                                                   AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC));
    AML_SD_EMMC_TRACE("   ERROR: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_ERROR));
    AML_SD_EMMC_TRACE("   OWNER: %d\n", get_bit(cmd_desc, AML_SD_EMMC_CMD_INFO_OWNER));
}


uint32_t AmlSdEmmc::GetClkFreq(uint32_t clk_src) const {
    if (clk_src == AML_SD_EMMC_FCLK_DIV2_SRC) {
        return AML_SD_EMMC_FCLK_DIV2_FREQ;
    }
    return AML_SD_EMMC_CTS_OSCIN_CLK_FREQ;
}

int AmlSdEmmc::IrqThread() {
    while (1) {
        uint32_t status_irq;
        zx::time timestamp;
        zx_status_t status = irq_.wait(&timestamp);
        if (status != ZX_OK) {
            zxlogf(ERROR, "AmlSdEmmc::IrqThread: zx_interrupt_wait got %d\n", status);
            break;
        }
        fbl::AutoLock mutex_al(&mtx_);
        if (cur_req_ == NULL) {
            status = ZX_ERR_IO_INVALID;
            zxlogf(ERROR, "AmlSdEmmc::IrqThread: Got a spurious interrupt\n");
            //TODO(ravoorir): Do some error recovery here and continue instead
            // of breaking.
            break;
        }

        status_irq = mmio_.Read32(AML_SD_EMMC_STATUS_OFFSET);

        uint32_t rxd_err = get_bits(status_irq, AML_SD_EMMC_STATUS_RXD_ERR_MASK,
                                    AML_SD_EMMC_STATUS_RXD_ERR_LOC);

        auto complete_ac = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
            cur_req_->status = status;
            mmio_.Write32(AML_SD_EMMC_IRQ_ALL_CLEAR, AML_SD_EMMC_STATUS_OFFSET);
            cur_req_ = nullptr;
            sync_completion_signal(&req_completion_);
        });

        if (rxd_err) {
            if (cur_req_->probe_tuning_cmd) {
                AML_SD_EMMC_TRACE("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n",
                                  cur_req_->cmd_idx, status_irq, rxd_err);
            } else {
                AML_SD_EMMC_ERROR("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n",
                                  cur_req_->cmd_idx, status_irq, rxd_err);
            }
            status = ZX_ERR_IO_DATA_INTEGRITY;
            continue;
        }
        if (status_irq & AML_SD_EMMC_STATUS_TXD_ERR) {
            AML_SD_EMMC_ERROR("TX Data CRC Error, cmd%d, status=0x%x TXD_ERR\n", cur_req_->cmd_idx,
                              status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            continue;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_ERR) {
            AML_SD_EMMC_ERROR("Controller does not own the descriptor, cmd%d, status=0x%x\n",
                              cur_req_->cmd_idx, status_irq);
            status = ZX_ERR_IO_INVALID;
            continue;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_ERR) {
            AML_SD_EMMC_ERROR("Response CRC Error, cmd%d, status=0x%x\n", cur_req_->cmd_idx,
                              status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            continue;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_TIMEOUT) {
            // When mmc dev_ice is being probed with SDIO command this is an expected failure.
            if (cur_req_->probe_tuning_cmd) {
                AML_SD_EMMC_TRACE("No response received before time limit, cmd%d, status=0x%x\n",
                                  cur_req_->cmd_idx, status_irq);
            } else {
                AML_SD_EMMC_ERROR("No response received before time limit, cmd%d, status=0x%x\n",
                                  cur_req_->cmd_idx, status_irq);
            }
            status = ZX_ERR_TIMED_OUT;
            continue;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_TIMEOUT) {
            AML_SD_EMMC_ERROR("Descriptor execution timed out, cmd%d, status=0x%x\n",
                              cur_req_->cmd_idx, status_irq);
            status = ZX_ERR_TIMED_OUT;
            continue;
        }

        if (!(status_irq & AML_SD_EMMC_STATUS_END_OF_CHAIN)) {
            status = ZX_ERR_IO_INVALID;
            zxlogf(ERROR, "AmlSdEmmc::IrqThread: END OF CHAIN bit is not set status:0x%x\n",
                   status_irq);
            continue;
        }

        if (cur_req_->cmd_flags & SDMMC_RESP_LEN_136) {
            cur_req_->response[0] = mmio_.Read32(AML_SD_EMMC_CMD_RSP_OFFSET);
            cur_req_->response[1] = mmio_.Read32(AML_SD_EMMC_CMD_RSP1_OFFSET);
            cur_req_->response[2] = mmio_.Read32(AML_SD_EMMC_CMD_RSP2_OFFSET);
            cur_req_->response[3] = mmio_.Read32(AML_SD_EMMC_CMD_RSP3_OFFSET);
        } else {
            cur_req_->response[0] = mmio_.Read32(AML_SD_EMMC_CMD_RSP_OFFSET);
        }
        if ((!cur_req_->use_dma) && (cur_req_->cmd_flags & SDMMC_CMD_READ)) {
            uint32_t length = cur_req_->blockcount * cur_req_->blocksize;
            if (length == 0 || ((length % 4) != 0)) {
                status = ZX_ERR_INTERNAL;
                continue;
            }
            uint32_t data_copied = 0;
            uint32_t* dest = reinterpret_cast<uint32_t*>(cur_req_->virt_buffer);
            volatile uint32_t* src =
                reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(mmio_.get()) +
                                                     AML_SD_EMMC_PING_BUFFER_BASE);
            while (length) {
                *dest++ = *src++;
                length -= 4;
                data_copied += 4;
            }
        }
    }
    return 0;
}


zx_status_t AmlSdEmmc::SdmmcHostInfo(sdmmc_host_info_t* info) {
    memcpy(info, &dev_info_, sizeof(dev_info_));
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcSetBusWidth(sdmmc_bus_width_t bw) {
    uint32_t config = mmio_.Read32(AML_SD_EMMC_CFG_OFFSET);

    switch (bw) {
    case SDMMC_BUS_WIDTH_ONE:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_1BIT);
        break;
    case SDMMC_BUS_WIDTH_FOUR:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_4BIT);
        break;
    case SDMMC_BUS_WIDTH_EIGHT:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_8BIT);
        break;
    default:
        return ZX_ERR_OUT_OF_RANGE;
    }

    mmio_.Write32(config, AML_SD_EMMC_CFG_OFFSET);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcGetInBandInterrupt(zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlSdEmmc::SdmmcSetBusFreq(uint32_t freq) {
    uint32_t clk = 0, clk_src = 0, clk_div = 0;
    uint32_t clk_val = mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET);
    if (freq == 0) {
        //TODO: Disable clock here
        return ZX_ERR_NOT_SUPPORTED;
    } else if (freq > max_freq_) {
        freq = max_freq_;
    } else if (freq < min_freq_) {
        freq = min_freq_;
    }
    if (freq < AML_SD_EMMC_FCLK_DIV2_MIN_FREQ) {
        clk_src = AML_SD_EMMC_CTS_OSCIN_CLK_SRC;
        clk = AML_SD_EMMC_CTS_OSCIN_CLK_FREQ;
    } else {
        clk_src = AML_SD_EMMC_FCLK_DIV2_SRC;
        clk = AML_SD_EMMC_FCLK_DIV2_FREQ;
    }
    clk_div = clk / freq;
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC, clk_div);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC, clk_src);
    mmio_.Write32(clk_val, AML_SD_EMMC_CLOCK_OFFSET);
    return ZX_OK;
}

void AmlSdEmmc::ConfigureDefaultRegs() {
    uint32_t config = 0;
    uint32_t clk_val = 0;
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC, AML_SD_EMMC_DEFAULT_CLK_CORE_PHASE);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC,
                AML_SD_EMMC_DEFAULT_CLK_SRC);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                AML_SD_EMMC_DEFAULT_CLK_DIV);
    clk_val |= AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON;

    mmio_.Write32(clk_val, AML_SD_EMMC_CLOCK_OFFSET);

    update_bits(&config, AML_SD_EMMC_CFG_BL_LEN_MASK, AML_SD_EMMC_CFG_BL_LEN_LOC,
                AML_SD_EMMC_DEFAULT_BL_LEN);
    update_bits(&config, AML_SD_EMMC_CFG_RESP_TIMEOUT_MASK, AML_SD_EMMC_CFG_RESP_TIMEOUT_LOC,
                AML_SD_EMMC_DEFAULT_RESP_TIMEOUT);
    update_bits(&config, AML_SD_EMMC_CFG_RC_CC_MASK, AML_SD_EMMC_CFG_RC_CC_LOC,
                AML_SD_EMMC_DEFAULT_RC_CC);
    update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                AML_SD_EMMC_CFG_BUS_WIDTH_1BIT);

    mmio_.Write32(config, AML_SD_EMMC_CFG_OFFSET);
    mmio_.Write32(AML_SD_EMMC_IRQ_ALL_CLEAR, AML_SD_EMMC_STATUS_OFFSET);
    mmio_.Write32(AML_SD_EMMC_IRQ_ALL_CLEAR, AML_SD_EMMC_IRQ_EN_OFFSET);
}

void AmlSdEmmc::SdmmcHwReset() {
   if (reset_gpio_.is_valid()) {
        reset_gpio_.ConfigOut(0);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
        reset_gpio_.ConfigOut(1);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
   }
   ConfigureDefaultRegs();
}

zx_status_t AmlSdEmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
    uint32_t config = mmio_.Read32(AML_SD_EMMC_CFG_OFFSET);
    uint32_t clk_val = mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET);

    if (timing == SDMMC_TIMING_HS400 || timing == SDMMC_TIMING_HSDDR ||
        timing == SDMMC_TIMING_DDR50) {
        if (timing == SDMMC_TIMING_HS400) {
            config |= AML_SD_EMMC_CFG_CHK_DS;
        } else {
            config &= ~AML_SD_EMMC_CFG_CHK_DS;
        }
        config |= AML_SD_EMMC_CFG_DDR;
        uint32_t clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK,
                                    AML_SD_EMMC_CLOCK_CFG_DIV_LOC);
        if (clk_div & 0x01) {
            clk_div++;
        }
        clk_div /= 2;
        update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                    clk_div);
    } else {
        config &= ~AML_SD_EMMC_CFG_DDR;
    }

    mmio_.Write32(config, AML_SD_EMMC_CFG_OFFSET);
    mmio_.Write32(clk_val, AML_SD_EMMC_CLOCK_OFFSET);
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
    //Amlogic controller does not allow to modify voltage
    //We do not return an error here since things work fine without switching the voltage.
    return ZX_OK;
}

void AmlSdEmmc::SetupCmdDesc(sdmmc_req_t* req, aml_sd_emmc_desc_t** out_desc) {
    aml_sd_emmc_desc_t* desc;
    if (req->use_dma) {
        ZX_DEBUG_ASSERT((dev_info_.caps & SDMMC_HOST_CAP_ADMA2));
        desc = reinterpret_cast<aml_sd_emmc_desc_t*>(descs_buffer_.virt());
        memset(desc, 0, descs_buffer_.size());
    } else {
        desc = reinterpret_cast<aml_sd_emmc_desc_t*>(reinterpret_cast<uintptr_t>(mmio_.get()) +
                                                     AML_SD_EMMC_SRAM_MEMORY_BASE);
    }
    uint32_t cmd_info = 0;
    if (req->cmd_flags == 0) {
        cmd_info |= AML_SD_EMMC_CMD_INFO_NO_RESP;
    } else {
        if (req->cmd_flags & SDMMC_RESP_LEN_136) {
            cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_128;
        }

        if (!(req->cmd_flags & SDMMC_RESP_CRC_CHECK)) {
            cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_NO_CRC;
        }

        if (req->cmd_flags & SDMMC_RESP_LEN_48B) {
            cmd_info |= AML_SD_EMMC_CMD_INFO_R1B;
        }

        cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_NUM;
    }
    update_bits(&cmd_info, AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK, AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC,
                AML_SD_EMMC_COMMAND(req->cmd_idx));
    update_bits(&cmd_info, AML_SD_EMMC_CMD_INFO_TIMEOUT_MASK, AML_SD_EMMC_CMD_INFO_TIMEOUT_LOC,
                AML_SD_EMMC_DEFAULT_CMD_TIMEOUT);
    cmd_info &= ~AML_SD_EMMC_CMD_INFO_ERROR;
    cmd_info |= AML_SD_EMMC_CMD_INFO_OWNER;
    cmd_info &= ~AML_SD_EMMC_CMD_INFO_END_OF_CHAIN;
    desc->cmd_info = cmd_info;
    desc->cmd_arg = req->arg;
    desc->data_addr = 0;
    desc->resp_addr = 0;
    *out_desc = desc;
}

zx_status_t AmlSdEmmc::SetupDataDescsDma(sdmmc_req_t* req,
                                         aml_sd_emmc_desc_t* cur_desc,
                                         aml_sd_emmc_desc_t** last_desc) {
    uint64_t req_len = req->blockcount * req->blocksize;
    bool is_read = req->cmd_flags & SDMMC_CMD_READ;
    uint64_t pagecount = ((req->buf_offset & PAGE_MASK) + req_len + PAGE_MASK) /
                         PAGE_SIZE;
    if (pagecount > SDMMC_PAGES_COUNT) {
        zxlogf(ERROR, "AmlSdEmmc::SetupDataDescsDma: too many pages %lu vs %lu\n", pagecount,
               SDMMC_PAGES_COUNT);
        return ZX_ERR_INVALID_ARGS;
    }

    // pin the vmo
    zx_paddr_t phys[SDMMC_PAGES_COUNT];
    // offset_vmo is converted to bytes by the sdmmc layer
    uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

    zx_status_t st = zx_bti_pin(bti_.get(), options, req->dma_vmo,
                                req->buf_offset & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, phys, pagecount, &req->pmt);
    if (st != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::SetupDataDescsDma: bti-pin failed with error %d\n", st);
        return st;
    }

    auto unpin_ac = fbl::MakeAutoCall([&req]() { zx_pmt_unpin(req->pmt); });
    if (is_read) {
        st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                             req->buf_offset, req_len, NULL, 0);
    } else {
        st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN,
                             req->buf_offset, req_len, NULL, 0);
    }
    if (st != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::SetupDataDescsDma: cache clean failed with error  %d\n", st);
        return st;
    }

    phys_iter_buffer_t buf = {};
    buf.phys = phys;
    buf.phys_count = pagecount;
    buf.length = req_len;
    buf.vmo_offset = req->buf_offset;

    phys_iter_t iter;
    phys_iter_init(&iter, &buf, PAGE_SIZE);

    int count = 0;
    size_t length;
    zx_paddr_t paddr;
    uint16_t blockcount;
    aml_sd_emmc_desc_t* desc = cur_desc;
    for (;;) {
        length = phys_iter_next(&iter, &paddr);
        if (length == 0) {
            if (desc != descs_buffer_.virt()) {
                desc -= 1;
                *last_desc = desc;
                break;
            } else {
                zxlogf(TRACE, "AmlSdEmmc::SetupDataDescsDma: empty descriptor list!\n");
                return ZX_ERR_NOT_SUPPORTED;
            }
        } else if (length > PAGE_SIZE) {
            zxlogf(TRACE, "AmlSdEmmc::SetupDataDescsDma: chunk size > %zu is unsupported\n",
                   length);
            return ZX_ERR_NOT_SUPPORTED;
        } else if ((++count) > AML_DMA_DESC_MAX_COUNT) {
            zxlogf(TRACE, "AmlSdEmmc::SetupDataDescsDma: request with more than %d chunks "
                  "is unsupported\n", AML_DMA_DESC_MAX_COUNT);
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (count > 1) {
            desc->cmd_info |= AML_SD_EMMC_CMD_INFO_NO_RESP;
            desc->cmd_info |= AML_SD_EMMC_CMD_INFO_NO_CMD;
        }

        desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_IO;
        if (!(req->cmd_flags & SDMMC_CMD_READ)) {
            desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_WR;
        }
        desc->cmd_info |= AML_SD_EMMC_CMD_INFO_OWNER;
        update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_TIMEOUT_MASK,
                    AML_SD_EMMC_CMD_INFO_TIMEOUT_LOC, AML_SD_EMMC_DEFAULT_CMD_TIMEOUT);
        desc->cmd_info &= ~AML_SD_EMMC_CMD_INFO_ERROR;

        uint16_t blocksize = req->blocksize;
        blockcount = static_cast<uint16_t>(length / blocksize);
        ZX_DEBUG_ASSERT(((length % blocksize) == 0));

        if (blockcount > 1) {
            desc->cmd_info |= AML_SD_EMMC_CMD_INFO_BLOCK_MODE;
            update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                        AML_SD_EMMC_CMD_INFO_LEN_LOC, blockcount);
        } else {
            update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                        AML_SD_EMMC_CMD_INFO_LEN_LOC, req->blocksize);
        }

        desc->data_addr = static_cast<uint32_t>(paddr);
        desc += 1;
    }
    unpin_ac.cancel();
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SetupDataDescsPio(sdmmc_req_t* req,
                                         aml_sd_emmc_desc_t* desc,
                                         aml_sd_emmc_desc_t** last_desc) {
    zx_status_t status = ZX_OK;
    uint32_t length = req->blockcount * req->blocksize;

    if (length > AML_SD_EMMC_MAX_PIO_DATA_SIZE) {
        zxlogf(ERROR, "AmlSdEmmc::SetupDataDescsPio: Request transfer size is greater than "
               "max transfer size\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (length == 0 || ((length % 4) != 0)) {
        // From Amlogic documentation, Ping and Pong buffers in sram can be accessed only 4 bytes
        // at a time.
        zxlogf(ERROR, "AmlSdEmmc::SetupDataDescsPio: Request sizes that are not multiple of "
               "4 are not supported in PIO mode\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_IO;
    if (!(req->cmd_flags & SDMMC_CMD_READ)) {
        desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_WR;
        uint32_t data_copied = 0;
        uint32_t data_remaining = length;
        uint32_t* src = reinterpret_cast<uint32_t*>(req->virt_buffer);
        volatile uint32_t* dest =
            reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(mmio_.get()) +
                                                 AML_SD_EMMC_PING_BUFFER_BASE);
        while (data_remaining) {
            *dest++ = *src++;
            data_remaining -= 4;
            data_copied += 4;
        }
    }

    if (req->blockcount > 1) {
        desc->cmd_info |= AML_SD_EMMC_CMD_INFO_BLOCK_MODE;
        update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                    AML_SD_EMMC_CMD_INFO_LEN_LOC, req->blockcount);
    } else {
        update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                    AML_SD_EMMC_CMD_INFO_LEN_LOC, req->blocksize);
    }

    // data_addr[0] = 0 for DDR. data_addr[0] = 1 if address is from SRAM

    zx_paddr_t buffer_phys = pinned_mmio_.get_paddr() + AML_SD_EMMC_PING_BUFFER_BASE;
    desc->data_addr = static_cast<uint32_t>(buffer_phys | 1);
    *last_desc = desc;
    return status;
}

zx_status_t AmlSdEmmc::SetupDataDescs(sdmmc_req_t* req, aml_sd_emmc_desc_t* desc,
                                      aml_sd_emmc_desc_t** last_desc) {
    zx_status_t st = ZX_OK;

    if (!req->blocksize || req->blocksize > AML_SD_EMMC_MAX_BLK_SIZE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (req->use_dma) {
        st = SetupDataDescsDma(req, desc, last_desc);
        if (st != ZX_OK) {
            return st;
        }
    } else {
        st =  SetupDataDescsPio(req, desc, last_desc);
        if (st != ZX_OK) {
            return st;
        }
    }

    //update config
    uint32_t config = mmio_.Read32(AML_SD_EMMC_CFG_OFFSET);
    uint8_t cur_blk_len = static_cast<uint8_t>(get_bits(config, AML_SD_EMMC_CFG_BL_LEN_MASK,
                                   AML_SD_EMMC_CFG_BL_LEN_LOC));
    uint8_t req_blk_len = log2_ceil(req->blocksize);
    if (cur_blk_len != req_blk_len) {
        update_bits(&config, AML_SD_EMMC_CFG_BL_LEN_MASK, AML_SD_EMMC_CFG_BL_LEN_LOC,
                    req_blk_len);
        mmio_.Write32(config, AML_SD_EMMC_CFG_OFFSET);
    }
    return ZX_OK;
}

zx_status_t AmlSdEmmc::FinishReq(sdmmc_req_t* req) {
    zx_status_t st = ZX_OK;
    if (req->use_dma && req->pmt != ZX_HANDLE_INVALID) {
        /*
         * Clean the cache one more time after the DMA operation because there
         * might be a possibility of cpu prefetching while the DMA operation is
         * going on.
         */
        uint64_t req_len = req->blockcount * req->blocksize;
        if ((req->cmd_flags & SDMMC_CMD_READ) && req->use_dma) {
            st = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                 req->buf_offset, req_len, NULL, 0);
            if (st != ZX_OK) {
                zxlogf(ERROR, "AmlSdEmmc::FinishReq: cache clean failed with error  %d\n", st);
            }
        }

        st = zx_pmt_unpin(req->pmt);
        if (st != ZX_OK) {
            zxlogf(ERROR, "AmlSdEmmc::FinishReq: error %d in pmt_unpin\n", st);
        }
        req->pmt = ZX_HANDLE_INVALID;
    }

    return st;
}

zx_status_t AmlSdEmmc::SdmmcRequest(sdmmc_req_t* req) {
    zx_status_t status = ZX_OK;

    // stop executing
    uint32_t start_reg = mmio_.Read32(AML_SD_EMMC_START_OFFSET);
    start_reg &= ~AML_SD_EMMC_START_DESC_BUSY;
    mmio_.Write32(start_reg, AML_SD_EMMC_START_OFFSET);
    aml_sd_emmc_desc_t* desc;
    aml_sd_emmc_desc_t* last_desc;

    SetupCmdDesc(req, &desc);
    last_desc = desc;
    if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
        status = SetupDataDescs(req, desc, &last_desc);
        if (status != ZX_OK) {
            zxlogf(ERROR, "AmlSdEmmc::SdmmcRequest: Failed to setup data descriptors\n");
            return status;
        }
    }

    last_desc->cmd_info |= AML_SD_EMMC_CMD_INFO_END_OF_CHAIN;
    AML_SD_EMMC_TRACE("SUBMIT req:%p cmd_idx: %d cmd_cfg: 0x%x cmd_dat: 0x%x cmd_arg: 0x%x\n", req,
                      req->cmd_idx, desc->cmd_info, desc->data_addr, desc->cmd_arg);

    {
        fbl::AutoLock mutex_al(&mtx_);
        cur_req_ = req;
        zx_paddr_t desc_phys;

        start_reg = mmio_.Read32(AML_SD_EMMC_START_OFFSET);
        if (req->use_dma) {
            desc_phys = descs_buffer_.phys();
            descs_buffer_.CacheFlush(0, descs_buffer_.size());
            //Read desc from external DDR
            start_reg &= ~AML_SD_EMMC_START_DESC_INT;
        } else {
            desc_phys = pinned_mmio_.get_paddr() + AML_SD_EMMC_SRAM_MEMORY_BASE;
            start_reg |= AML_SD_EMMC_START_DESC_INT;
        }

        start_reg |= AML_SD_EMMC_START_DESC_BUSY;
        update_bits(&start_reg, AML_SD_EMMC_START_DESC_ADDR_MASK, AML_SD_EMMC_START_DESC_ADDR_LOC,
                    ((static_cast<uint32_t>(desc_phys)) >> 2));
        mmio_.Write32(start_reg, AML_SD_EMMC_START_OFFSET);
    }

    sync_completion_wait(&req_completion_, ZX_TIME_INFINITE);
    FinishReq(req);
    sync_completion_reset(&req_completion_);
    return req->status;
}

zx_status_t AmlSdEmmc::TuningDoTransfer(uint8_t* tuning_res, uint16_t blk_pattern_size,
                                        uint32_t tuning_cmd_idx) {
    sdmmc_req_t tuning_req = {};
    tuning_req.cmd_idx = tuning_cmd_idx;
    tuning_req.cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS;
    tuning_req.arg = 0;
    tuning_req.blockcount = 1;
    tuning_req.blocksize = blk_pattern_size;
    tuning_req.use_dma = false;
    tuning_req.virt_buffer = tuning_res;
    tuning_req.virt_size = blk_pattern_size;
    tuning_req.probe_tuning_cmd = true;
    return AmlSdEmmc::SdmmcRequest(&tuning_req);
}

bool AmlSdEmmc::TuningTestDelay(const uint8_t* blk_pattern, uint16_t blk_pattern_size,
                                uint32_t adj_delay, uint32_t tuning_cmd_idx) {
    uint32_t adjust_reg = mmio_.Read32(AML_SD_EMMC_ADJUST_OFFSET);
    update_bits(&adjust_reg, AML_SD_EMMC_ADJUST_ADJ_DELAY_MASK,
                AML_SD_EMMC_ADJUST_ADJ_DELAY_LOC, adj_delay);
    adjust_reg |= AML_SD_EMMC_ADJUST_ADJ_FIXED;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_RISE;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_ENABLE;
    mmio_.Write32(adjust_reg, AML_SD_EMMC_ADJUST_OFFSET);

    zx_status_t status = ZX_OK;
    size_t n;
    for (n = 0; n < AML_SD_EMMC_ADJ_DELAY_TEST_ATTEMPTS; n++) {
        uint8_t tuning_res[512] = {0};
        status = TuningDoTransfer(tuning_res, blk_pattern_size, tuning_cmd_idx);
        if (status != ZX_OK || memcmp(blk_pattern, tuning_res, blk_pattern_size)) {
            break;
        }
    }
    return (n == AML_SD_EMMC_ADJ_DELAY_TEST_ATTEMPTS);
}

zx_status_t AmlSdEmmc::TuningCalculateBestWindow(const uint8_t* tuning_blk,
                                                 uint16_t tuning_blk_size,
                                                 uint32_t cur_clk_div, int* best_start,
                                                 uint32_t* best_size, uint32_t tuning_cmd_idx) {
    int cur_win_start = -1, best_win_start = -1;
    uint32_t cycle_begin_win_size = 0, cur_win_size = 0, best_win_size = 0;

    for (uint32_t adj_delay = 0; adj_delay < cur_clk_div; adj_delay++) {
        if (TuningTestDelay(tuning_blk, tuning_blk_size, adj_delay,
                                          tuning_cmd_idx)) {
            if (cur_win_start < 0) {
                cur_win_start = static_cast<int>(adj_delay);
            }
            cur_win_size++;
        } else {
            if (cur_win_start >= 0) {
                if (best_win_start < 0) {
                    best_win_start = cur_win_start;
                    best_win_size = cur_win_size;
                } else if (best_win_size < cur_win_size) {
                    best_win_start = cur_win_start;
                    best_win_size = cur_win_size;
                }
                if (cur_win_start == 0) {
                    cycle_begin_win_size = cur_win_size;
                }
                cur_win_start = -1;
                cur_win_size = 0;
            }
        }
    }
    // Last delay is good
    if (cur_win_start >= 0) {
        if (best_win_start < 0) {
            best_win_start = cur_win_start;
            best_win_size = cur_win_size;
        } else if (cycle_begin_win_size > 0) {
            // Combine the cur window with the window starting next cycle
            if (cur_win_size + cycle_begin_win_size > best_win_size) {
                best_win_start = cur_win_start;
                best_win_size = cur_win_size + cycle_begin_win_size;
            }
        } else if (best_win_size < cur_win_size) {
            best_win_start = cur_win_start;
            best_win_size = cur_win_size;
        }
    }

    *best_start = best_win_start;
    *best_size = best_win_size;
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcPerformTuning(uint32_t tuning_cmd_idx) {
    const uint8_t* tuning_blk;
    uint16_t tuning_blk_size = 0;
    int best_win_start = -1;
    uint32_t best_win_size = 0;
    uint32_t tries = 0;

    uint32_t config = mmio_.Read32(AML_SD_EMMC_CFG_OFFSET);
    uint32_t bw = get_bits(config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC);
    if (bw == AML_SD_EMMC_CFG_BUS_WIDTH_4BIT) {
        tuning_blk = aml_sd_emmc_tuning_blk_pattern_4bit;
        tuning_blk_size = sizeof(aml_sd_emmc_tuning_blk_pattern_4bit);
    } else if (bw == AML_SD_EMMC_CFG_BUS_WIDTH_8BIT) {
        tuning_blk = aml_sd_emmc_tuning_blk_pattern_8bit;
        tuning_blk_size = sizeof(aml_sd_emmc_tuning_blk_pattern_8bit);
    } else {
        zxlogf(ERROR, "AmlSdEmmc::SdmmcPerformTuning: Tuning at wrong buswidth: %d\n", bw);
        return ZX_ERR_INTERNAL;
    }

    uint32_t clk_val, clk_div;
    clk_val = mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET);
    clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC);

    do {
        TuningCalculateBestWindow(tuning_blk, tuning_blk_size, clk_div, &best_win_start,
                                  &best_win_size, tuning_cmd_idx);
        if (best_win_size == 0) {
            // Lower the frequency and try again
            zxlogf(INFO, "AmlSdEmmc::SdmmcPerformTuning: Tuning failed. Reducing the frequency "
                   "and trying again\n");
            clk_val = mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET);
            clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK,
                               AML_SD_EMMC_CLOCK_CFG_DIV_LOC);
            clk_div += 2;
            if (clk_div > (AML_SD_EMMC_CLOCK_CFG_DIV_MASK >> AML_SD_EMMC_CLOCK_CFG_DIV_LOC)) {
                clk_div = AML_SD_EMMC_CLOCK_CFG_DIV_MASK >> AML_SD_EMMC_CLOCK_CFG_DIV_LOC;
            }
            update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                        clk_div);
            mmio_.Write32(clk_val, AML_SD_EMMC_CLOCK_OFFSET);
            uint32_t clk_src = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK,
                                        AML_SD_EMMC_CLOCK_CFG_SRC_LOC);
            uint32_t cur_freq = (GetClkFreq(clk_src)) / clk_div;
            if (max_freq_ > cur_freq) {
                // Update max freq accordingly
                max_freq_ = cur_freq;
            }
        }
    } while (best_win_size == 0 && ++tries < AML_SD_EMMC_MAX_TUNING_TRIES);

    if (best_win_size == 0) {
        zxlogf(ERROR, "AmlSdEmmc::SdmmcPerformTuning: Tuning failed after :%d retries. "
               "Giving up.\n", AML_SD_EMMC_MAX_TUNING_TRIES);
        return ZX_ERR_IO;
    }

    uint32_t best_adj_delay = 0;
    uint32_t adjust_reg = mmio_.Read32(AML_SD_EMMC_ADJUST_OFFSET);

    clk_val = mmio_.Read32(AML_SD_EMMC_CLOCK_OFFSET);
    clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC);
    if (best_win_size != clk_div) {
        best_adj_delay = best_win_start + ((best_win_size - 1) / 2) + ((best_win_size - 1) % 2);
        best_adj_delay = best_adj_delay % clk_div;
    }
    update_bits(&adjust_reg, AML_SD_EMMC_ADJUST_ADJ_DELAY_MASK, AML_SD_EMMC_ADJUST_ADJ_DELAY_LOC,
                best_adj_delay);
    adjust_reg |= AML_SD_EMMC_ADJUST_ADJ_FIXED;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_RISE;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_ENABLE;
    mmio_.Write32(adjust_reg, AML_SD_EMMC_ADJUST_OFFSET);
    return ZX_OK;
}

zx_status_t AmlSdEmmc::Init() {
    dev_info_.caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330;
    if (board_config_.supports_dma) {
        dev_info_.caps |= SDMMC_HOST_CAP_ADMA2;
        zx_status_t status = descs_buffer_.Init(bti_.get(),
                                                AML_DMA_DESC_MAX_COUNT * sizeof(aml_sd_emmc_desc_t),
                                                IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            zxlogf(ERROR, "AmlSdEmmc::Init: Failed to allocate dma descriptors\n");
            return status;
        }
        dev_info_.max_transfer_size = AML_DMA_DESC_MAX_COUNT * PAGE_SIZE;
    } else {
        dev_info_.max_transfer_size = AML_SD_EMMC_MAX_PIO_DATA_SIZE;
    }

    dev_info_.max_transfer_size_non_dma = AML_SD_EMMC_MAX_PIO_DATA_SIZE;
    max_freq_ = board_config_.max_freq;
    min_freq_ = board_config_.min_freq;
    sync_completion_reset(&req_completion_);

    // Init the Irq thread
    auto cb = [](void* arg) -> int { return reinterpret_cast<AmlSdEmmc*>(arg)->IrqThread(); };
    if (thrd_create_with_name(&irq_thread_, cb, this, "aml_sd_emmc_irq_thread") != thrd_success) {
        zxlogf(ERROR, "AmlSdEmmc::Init: Failed to init irq thread\n");
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t AmlSdEmmc::Bind() {
    zx_status_t status = DdkAdd("aml-sd-emmc");
    if (status != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Bind: DdkAdd failed\n");
    }
    return status;
}

zx_status_t AmlSdEmmc::Create(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Could not get pdev: %d\n", status);
        return ZX_ERR_NO_RESOURCES;
    }

    zx::bti bti;
    if ((status = pdev.GetBti(0, &bti)) != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get BTI: %d\n", status);
        return status;
    }

    std::optional<ddk::MmioBuffer> mmio;
    status = pdev.MapMmio(0, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get mmio: %d\n", status);
        return status;
    }

    //Pin the mmio
    std::optional<ddk::MmioPinnedBuffer> pinned_mmio;
    status = mmio->Pin(bti, &pinned_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to pin mmio: %d\n", status);
        return status;
    }

    // Populate board specific information
    aml_sd_emmc_config_t config;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_EMMC_CONFIG, &config, sizeof(config),
                                 &actual);
    if (status != ZX_OK || actual != sizeof(config)) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get metadata: %d\n", status);
        return status;
    }

    zx::interrupt irq;
    if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get interrupt: %d\n", status);
        return status;
    }

    pdev_device_info_t dev_info;
    if ((status = pdev.GetDeviceInfo(&dev_info)) != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get device info: %d\n", status);
        return status;
    }

    ddk::GpioProtocolClient reset_gpio;
    if (dev_info.gpio_count > 0) {
        reset_gpio = pdev.GetGpio(0);
        if (!reset_gpio.is_valid()) {
            zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get GPIO\n");
            return ZX_ERR_NO_RESOURCES;
        }
    }

    auto dev = std::make_unique<AmlSdEmmc>(parent, pdev, std::move(bti), *std::move(mmio),
                                           *std::move(pinned_mmio),
                                           config, std::move(irq), reset_gpio);

    if ((status = dev->Init()) != ZX_OK) {
        return status;
    }

    if ((status = dev->Bind()) != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

AmlSdEmmc::~AmlSdEmmc() {
    irq_.destroy();
    if (irq_thread_)
        thrd_join(irq_thread_, NULL);
}

void AmlSdEmmc::DdkUnbind() {
    DdkRemove();
}

void AmlSdEmmc::DdkRelease() {
    irq_.destroy();
    if (irq_thread_)
        thrd_join(irq_thread_, NULL);
    delete this;
}

static zx_driver_ops_t aml_sd_emmc_driver_ops = []() {
    zx_driver_ops_t driver_ops;
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind =  AmlSdEmmc::Create;
    return driver_ops;
}();

} // sdmmc

ZIRCON_DRIVER_BEGIN(aml_sd_emmc, sdmmc::aml_sd_emmc_driver_ops, "zircon", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SD_EMMC_A),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SD_EMMC_B),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SD_EMMC_C),
ZIRCON_DRIVER_END(aml_sd_emmc)
