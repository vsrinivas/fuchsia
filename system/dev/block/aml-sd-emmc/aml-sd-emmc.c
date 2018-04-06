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
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/io-buffer.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <ddk/protocol/sdmmc.h>
#include <hw/sdmmc.h>

#include <zircon/assert.h>
#include <zircon/types.h>

#define DMA_DESC_COUNT              512
#define AML_SD_EMMC_TRACE(fmt, ...) zxlogf(TRACE, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_INFO(fmt, ...)  zxlogf(INFO, "%s: "fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_ERROR(fmt, ...) zxlogf(ERROR, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_COMMAND(c)      ((0x80) | (c))

typedef struct aml_sd_emmc_t {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    gpio_protocol_t gpio;
    uint32_t gpio_count;

    io_buffer_t mmio;

    // virt address of mmio
    aml_sd_emmc_regs_t* regs;

    zx_handle_t bti;
    io_buffer_t data_buffer;

    // Controller info
    sdmmc_host_info_t info;

    // current descritptor
    aml_sd_emmc_desc_t cur_desc;
} aml_sd_emmc_t;

static void aml_sd_emmc_dump_regs(aml_sd_emmc_t* dev) {
    aml_sd_emmc_regs_t* regs = dev->regs;
    AML_SD_EMMC_TRACE("sd_emmc_clock : 0x%x\n", regs->sd_emmc_clock);
    AML_SD_EMMC_TRACE("sd_emmc_delay1 : 0x%x\n", regs->sd_emmc_delay1);
    AML_SD_EMMC_TRACE("sd_emmc_delay2 : 0x%x\n", regs->sd_emmc_delay2);
    AML_SD_EMMC_TRACE("sd_emmc_adjust : 0x%x\n", regs->sd_emmc_adjust);
    AML_SD_EMMC_TRACE("sd_emmc_calout : 0x%x\n", regs->sd_emmc_calout);
    AML_SD_EMMC_TRACE("sd_emmc_start : 0x%x\n", regs->sd_emmc_start);
    AML_SD_EMMC_TRACE("sd_emmc_cfg : 0x%x\n", regs->sd_emmc_cfg);
    AML_SD_EMMC_TRACE("sd_emmc_status : 0x%x\n", regs->sd_emmc_status);
    AML_SD_EMMC_TRACE("sd_emmc_irq_en : 0x%x\n", regs->sd_emmc_irq_en);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_cfg : 0x%x\n", regs->sd_emmc_cmd_cfg);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_arg : 0x%x\n", regs->sd_emmc_cmd_arg);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_dat : 0x%x\n", regs->sd_emmc_cmd_dat);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp : 0x%x\n", regs->sd_emmc_cmd_rsp);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp1 : 0x%x\n", regs->sd_emmc_cmd_rsp1);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp2 : 0x%x\n", regs->sd_emmc_cmd_rsp2);
    AML_SD_EMMC_TRACE("sd_emmc_cmd_rsp3 : 0x%x\n", regs->sd_emmc_cmd_rsp3);
    AML_SD_EMMC_TRACE("bus_err : 0x%x\n", regs->bus_err);
    AML_SD_EMMC_TRACE("sd_emmc_curr_cfg: 0x%x\n", regs->sd_emmc_curr_cfg);
    AML_SD_EMMC_TRACE("sd_emmc_curr_arg: 0x%x\n", regs->sd_emmc_curr_arg);
    AML_SD_EMMC_TRACE("sd_emmc_curr_dat: 0x%x\n", regs->sd_emmc_curr_dat);
    AML_SD_EMMC_TRACE("sd_emmc_curr_rsp: 0x%x\n", regs->sd_emmc_curr_rsp);
    AML_SD_EMMC_TRACE("sd_emmc_next_cfg: 0x%x\n", regs->sd_emmc_curr_cfg);
    AML_SD_EMMC_TRACE("sd_emmc_next_arg: 0x%x\n", regs->sd_emmc_curr_arg);
    AML_SD_EMMC_TRACE("sd_emmc_next_dat: 0x%x\n", regs->sd_emmc_curr_dat);
    AML_SD_EMMC_TRACE("sd_emmc_next_rsp: 0x%x\n", regs->sd_emmc_curr_rsp);
    AML_SD_EMMC_TRACE("sd_emmc_rxd : 0x%x\n", regs->sd_emmc_rxd);
    AML_SD_EMMC_TRACE("sd_emmc_txd : 0x%x\n", regs->sd_emmc_txd);
    AML_SD_EMMC_TRACE("sramDesc : %p\n",regs->sramDesc);
    AML_SD_EMMC_TRACE("ping : %p\n", regs->ping);
    AML_SD_EMMC_TRACE("pong : %p\n", regs->pong);
}

static void aml_sd_emmc_dump_status(uint32_t status) {
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

static void aml_sd_emmc_dump_cfg(uint32_t config) {
    AML_SD_EMMC_TRACE("Dumping sd_emmc_cfg 0x%0x\n", config);
    AML_SD_EMMC_TRACE("    BUS_WIDTH: %d\n", get_bits(config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK,
                                                AML_SD_EMMC_CFG_BUS_WIDTH_LOC));
    AML_SD_EMMC_TRACE("    DDR: %d\n", get_bit(config, AML_SD_EMMC_CFG_DDR));
    AML_SD_EMMC_TRACE("    DC_UGT: %d\n", get_bit(config, AML_SD_EMMC_CFG_DC_UGT));
    AML_SD_EMMC_TRACE("    BLOCK LEN: %d\n", get_bits(config, AML_SD_EMMC_CFG_BL_LEN_MASK,
                                                AML_SD_EMMC_CFG_BL_LEN_LOC));
}

static void aml_sd_emmc_dump_clock(uint32_t clock) {
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

static void aml_sd_emmc_release(void* ctx) {
    aml_sd_emmc_t* dev = ctx;
    io_buffer_release(&dev->mmio);
    io_buffer_release(&dev->data_buffer);
    zx_handle_close(dev->bti);
    free(dev);
}

static zx_status_t aml_sd_emmc_host_info(void* ctx, sdmmc_host_info_t* info) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    memcpy(info, &dev->info, sizeof(dev->info));
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_bus_width(void* ctx, uint32_t bw) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    aml_sd_emmc_regs_t* regs = dev->regs;
    uint32_t config = regs->sd_emmc_cfg;

    switch (bw) {
    case SDMMC_BUS_WIDTH_1:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_1BIT);
        break;
    case SDMMC_BUS_WIDTH_4:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_4BIT);
        break;
    case SDMMC_BUS_WIDTH_8:
        update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                    AML_SD_EMMC_CFG_BUS_WIDTH_8BIT);
        break;
    default:
        return ZX_ERR_OUT_OF_RANGE;
    }

    regs->sd_emmc_cfg = config;
    return ZX_OK;
}

static void aml_sd_emmc_hw_reset(void* ctx) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    if (dev->gpio_count == 1) {
        //Currently we only have 1 gpio
        gpio_config(&dev->gpio, 0, GPIO_DIR_OUT);
        gpio_write(&dev->gpio, 0, 0);
        usleep(10 * 1000);
        gpio_write(&dev->gpio, 0, 1);
    }
    aml_sd_emmc_set_bus_width(ctx, 0);
}

static zx_status_t aml_sd_emmc_perform_tuning(void* ctx) {
    //TODO: Do the tuning here
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_bus_freq(void* ctx, uint32_t freq) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    aml_sd_emmc_regs_t* regs = dev->regs;

    uint32_t clk = 0, clk_src = 0, clk_div = 0;
    uint32_t clk_val = regs->sd_emmc_clock;

    uint32_t config = regs->sd_emmc_cfg;

    if (freq == 0) {
        //TODO: Disable clock here
    } else if (freq > AML_SD_EMMC_MAX_FREQ) {
        freq = AML_SD_EMMC_MAX_FREQ;
    } else if (freq < AML_SD_EMMC_MIN_FREQ) {
        freq = AML_SD_EMMC_MIN_FREQ;
    }

    if (freq < AML_SD_EMMC_FCLK_DIV2_MIN_FREQ) {
        clk_src = AML_SD_EMMC_CTS_OSCIN_CLK_SRC;
        clk = AML_SD_EMMC_CTS_OSCIN_CLK_FREQ;
    } else {
        clk_src = AML_SD_EMMC_FCLK_DIV2_SRC;
        clk = AML_SD_EMMC_FCLK_DIV2_FREQ;
    }

    clk_div = clk/freq;

    if (get_bit(config, AML_SD_EMMC_CFG_DDR)) {
        if (clk_div & 0x01) {
            clk_div++;
        }
        clk_div /= 2;
    }

    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC, clk_div);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC, clk_src);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC, 2);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_RX_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_RX_PHASE_LOC, 0);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_TX_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_TX_PHASE_LOC, 2);
    clk_val |= AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON;

    regs->sd_emmc_clock = clk_val;
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_bus_timing(void* ctx, sdmmc_timing_t timing) {
    aml_sd_emmc_t* dev = ctx;
    aml_sd_emmc_regs_t* regs = dev->regs;

    uint32_t config = regs->sd_emmc_cfg;

    if (timing == SDMMC_TIMING_HS400 || timing == SDMMC_TIMING_HSDDR) {
        if (timing == SDMMC_TIMING_HS400) {
            config |= AML_SD_EMMC_CFG_CHK_DS;
        } else {
            config &= ~AML_SD_EMMC_CFG_CHK_DS;
        }
        config |= AML_SD_EMMC_CFG_DDR;
    } else {
        config &= ~AML_SD_EMMC_CFG_DDR;
    }

    regs->sd_emmc_cfg = config;
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_signal_voltage(void* ctx, sdmmc_voltage_t voltage) {
    //Amlogic controller does not allow to modify voltage
    //We do not return an error here since things work fine without switching the voltage.
    return ZX_OK;
}

zx_status_t aml_sd_emmc_request(void *ctx, sdmmc_req_t* req) {
    uint32_t status_irq;
    uint32_t cmd = 0;
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    aml_sd_emmc_desc_t* desc = &(dev->cur_desc);
    aml_sd_emmc_regs_t* regs = dev->regs;

    memset(desc, 0, sizeof(*desc));
    desc->cmd_arg = req->arg;
    if (req->cmd_flags == 0) {
        cmd |= AML_SD_EMMC_CMD_INFO_NO_RESP;
    } else {
        if (req->cmd_flags & SDMMC_RESP_LEN_136) {
            cmd |= AML_SD_EMMC_CMD_INFO_RESP_128;
        }

        if (!(req->cmd_flags & SDMMC_RESP_CRC_CHECK)){
            cmd |= AML_SD_EMMC_CMD_INFO_RESP_NO_CRC;
        }
        desc->resp_addr = (unsigned long)req->response;
    }

    if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
        cmd |= AML_SD_EMMC_CMD_INFO_DATA_IO;
        zx_paddr_t buffer_phys = io_buffer_phys(&dev->data_buffer);

        if (!(req->cmd_flags & SDMMC_CMD_READ)) {
            cmd |= AML_SD_EMMC_CMD_INFO_DATA_WR;
            memcpy((void *)(io_buffer_virt(&dev->data_buffer)), req->virt,
                   req->blockcount * req->blocksize);
            io_buffer_cache_flush(&dev->data_buffer, 0, dev->info.max_transfer_size);
        } else if (req->cmd_flags & SDMMC_CMD_READ) {
            io_buffer_cache_flush_invalidate(&dev->data_buffer, 0, dev->info.max_transfer_size);
        }

        if (req->blockcount > 1) {
            cmd |= AML_SD_EMMC_CMD_INFO_BLOCK_MODE;
            update_bits(&cmd, AML_SD_EMMC_CMD_INFO_LEN_MASK, AML_SD_EMMC_CMD_INFO_LEN_LOC,
                        req->blockcount);
        } else{
            update_bits(&cmd, AML_SD_EMMC_CMD_INFO_LEN_MASK, AML_SD_EMMC_CMD_INFO_LEN_LOC,
                        req->blocksize);
        }
        desc->data_addr = (uint32_t)buffer_phys;
        // data_addr[0] = 0 for DDR. data_addr[0] = 1 if address is from SRAM
        // Our address comes from DDR. However we do not need to clear this,
        // Since physical address is always page-aligned.
        //desc->data_addr &= ~(1 << 0);
        ZX_DEBUG_ASSERT(!(desc->data_addr & 1));
    }

    update_bits(&cmd, AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK, AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC,
                AML_SD_EMMC_COMMAND(req->cmd_idx));
    cmd |= AML_SD_EMMC_CMD_INFO_OWNER;
    cmd |= AML_SD_EMMC_CMD_INFO_END_OF_CHAIN;
    desc->cmd_info = cmd;

    // TODO(ravoorir): Use DMA descriptors to queue multiple commands
    AML_SD_EMMC_TRACE("SUBMIT cmd_idx: %d cmd_cfg: 0x%x cmd_dat: 0x%x cmd_arg: 0x%x\n",
                get_bits(cmd, AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK, AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC),
                desc->cmd_info, desc->data_addr, desc->cmd_arg);
    regs->sd_emmc_status = AML_SD_EMMC_IRQ_ALL_CLEAR;
    regs->sd_emmc_cmd_cfg = desc->cmd_info;
    regs->sd_emmc_cmd_dat = desc->data_addr;
    regs->sd_emmc_cmd_arg = desc->cmd_arg;

    // TODO(ravoorir): Complete requests asynchronously on a different thread.
    while (1) {
        status_irq = regs->sd_emmc_status;
        if (status_irq & AML_SD_EMMC_STATUS_END_OF_CHAIN) {
            break;
        }
    }

    uint32_t rxd_err = get_bits(status_irq, AML_SD_EMMC_STATUS_RXD_ERR_MASK,
                                AML_SD_EMMC_STATUS_RXD_ERR_LOC);
    if (rxd_err) {
        AML_SD_EMMC_ERROR("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n", req->cmd_idx,
                          status_irq, rxd_err);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (status_irq & AML_SD_EMMC_STATUS_TXD_ERR) {
        AML_SD_EMMC_ERROR("TX Data CRC Error, cmd%d, status=0x%x TXD_ERR\n", req->cmd_idx,
                          status_irq);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (status_irq & AML_SD_EMMC_STATUS_DESC_ERR) {
        AML_SD_EMMC_ERROR("Controller does not own the descriptor, cmd%d, status=0x%x\n",
                          req->cmd_idx, status_irq);
        return ZX_ERR_IO_INVALID;
    }
    if (status_irq & AML_SD_EMMC_STATUS_RESP_ERR) {
        AML_SD_EMMC_ERROR("Response CRC Error, cmd%d, status=0x%x\n", req->cmd_idx, status_irq);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (status_irq & AML_SD_EMMC_STATUS_RESP_TIMEOUT) {
        AML_SD_EMMC_ERROR("No response reived before time limit, cmd%d, status=0x%x\n",
                req->cmd_idx, status_irq);
        return ZX_ERR_TIMED_OUT;
    }
    if (status_irq & AML_SD_EMMC_STATUS_DESC_TIMEOUT) {
        AML_SD_EMMC_ERROR("Descriptor execution timed out, cmd%d, status=0x%x\n", req->cmd_idx,
                    status_irq);
        return ZX_ERR_TIMED_OUT;
    }
    if (status_irq & AML_SD_EMMC_STATUS_BUS_CORE_BUSY) {
        AML_SD_EMMC_ERROR("Core is busy, cmd%d, status=0x%x\n", req->cmd_idx, status_irq);
        return ZX_ERR_SHOULD_WAIT;
    }

    if (req->cmd_flags & SDMMC_RESP_LEN_136) {
        req->response[0] = regs->sd_emmc_cmd_rsp;
        req->response[1] = regs->sd_emmc_cmd_rsp1;
        req->response[2] = regs->sd_emmc_cmd_rsp2;
        req->response[3] = regs->sd_emmc_cmd_rsp3;
    } else {
        req->response[0] = regs->sd_emmc_cmd_rsp;
    }
    if (req->cmd_flags & SDMMC_CMD_READ) {
        memcpy(req->virt, (void *)(io_buffer_virt(&dev->data_buffer)),
               req->blockcount * req->blocksize);
    }

    return ZX_OK;
}

static zx_protocol_device_t dev_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_sd_emmc_release,
};

static sdmmc_protocol_ops_t aml_sdmmc_proto = {
    .host_info = aml_sd_emmc_host_info,
    .set_signal_voltage = aml_sd_emmc_set_signal_voltage,
    .set_bus_width = aml_sd_emmc_set_bus_width,
    .set_bus_freq = aml_sd_emmc_set_bus_freq,
    .set_timing = aml_sd_emmc_set_bus_timing,
    .hw_reset = aml_sd_emmc_hw_reset,
    .perform_tuning = aml_sd_emmc_perform_tuning,
    .request = aml_sd_emmc_request,
};

static zx_status_t aml_sd_emmc_bind(void* ctx, zx_device_t* parent) {
    aml_sd_emmc_t* dev = calloc(1, sizeof(aml_sd_emmc_t));
    if (!dev) {
        zxlogf(ERROR, "aml-dev_bind: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = ZX_OK;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &dev->pdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &dev->gpio)) != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: ZX_PROTOCOL_GPIO not available\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&dev->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: pdev_get_device_info failed\n");
        goto fail;
    }
    if (info.mmio_count != info.irq_count) {
         zxlogf(ERROR, "aml_sd_emmc_bind: mmio_count %u does not match irq_count %u\n",
               info.mmio_count, info.irq_count);
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    dev->gpio_count = info.gpio_count;
    dev->info.caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330 |
                       SDMMC_HOST_CAP_ADMA2;
    //TODO(ravoorir): This is set arbitrarily for now.
    //Set it to max num of DMA desc * PAGE_SIZE  when implementing DMA descriptors
    dev->info.max_transfer_size = 2 * PAGE_SIZE;

    status = pdev_get_bti(&dev->pdev, 0, &dev->bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: pdev_get_bti failed\n");
        goto fail;
    }

    status = pdev_map_mmio_buffer(&dev->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &dev->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: pdev_map_mmio_buffer failed %d\n", status);
        goto fail;
    }
    dev->regs = (aml_sd_emmc_regs_t*)io_buffer_virt(&dev->mmio);

    status = io_buffer_init(&dev->data_buffer, dev->bti, dev->info.max_transfer_size,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: Failed to initiate data buffer\n");
        goto fail;
    }
    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-sd-emmc",
        .ctx = dev,
        .ops = &dev_device_proto,
        .proto_id = ZX_PROTOCOL_SDMMC,
        .proto_ops = &aml_sdmmc_proto,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }
    return ZX_OK;

fail:
    aml_sd_emmc_release(dev);
    return status;
}


static zx_driver_ops_t aml_sd_emmc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_sd_emmc_bind,
};

ZIRCON_DRIVER_BEGIN(aml_sd_emmc, aml_sd_emmc_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SD_EMMC),
ZIRCON_DRIVER_END(aml_sd_emmc)
