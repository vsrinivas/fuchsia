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
#include <ddk/metadata.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <ddk/protocol/sdmmc.h>
#include <hw/sdmmc.h>
#include <sync/completion.h>

#include <zircon/assert.h>
#include <zircon/types.h>
#include <zircon/threads.h>

// Limit maximum number of descriptors to 512 for now
#define AML_DMA_DESC_MAX_COUNT      512
#define AML_SD_EMMC_TRACE(fmt, ...) zxlogf(TRACE, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_INFO(fmt, ...)  zxlogf(INFO, "%s: "fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_ERROR(fmt, ...) zxlogf(ERROR, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_COMMAND(c)      ((0x80) | (c))
#define PAGE_MASK                   (PAGE_SIZE - 1ull)

typedef struct aml_sd_emmc_t {
    platform_device_protocol_t pdev;
    zx_device_t* zxdev;
    gpio_protocol_t gpio;
    uint32_t gpio_count;
    io_buffer_t mmio;
    // virt address of mmio
    aml_sd_emmc_regs_t* regs;
    zx_handle_t irq_handle;
    thrd_t irq_thread;
    zx_handle_t bti;
    io_buffer_t descs_buffer;
    // Held when I/O submit/complete is in progress.
    mtx_t mtx;
    // Controller info
    sdmmc_host_info_t info;
    uint32_t max_freq;
    uint32_t min_freq;
    // cur pending req
    sdmmc_req_t *cur_req;
    // used to signal request complete
    completion_t req_completion;
} aml_sd_emmc_t;

zx_status_t aml_sd_emmc_request(void *ctx, sdmmc_req_t* req);
static void aml_sd_emmc_dump_clock(uint32_t clock);
static void aml_sd_emmc_dump_cfg(uint32_t cfg);

static void aml_sd_emmc_dump_regs(aml_sd_emmc_t* dev) {
    aml_sd_emmc_regs_t* regs = dev->regs;
    AML_SD_EMMC_TRACE("sd_emmc_clock : 0x%x\n", regs->sd_emmc_clock);
    aml_sd_emmc_dump_clock(regs->sd_emmc_clock);
    AML_SD_EMMC_TRACE("sd_emmc_delay1 : 0x%x\n", regs->sd_emmc_delay1);
    AML_SD_EMMC_TRACE("sd_emmc_delay2 : 0x%x\n", regs->sd_emmc_delay2);
    AML_SD_EMMC_TRACE("sd_emmc_adjust : 0x%x\n", regs->sd_emmc_adjust);
    AML_SD_EMMC_TRACE("sd_emmc_calout : 0x%x\n", regs->sd_emmc_calout);
    AML_SD_EMMC_TRACE("sd_emmc_start : 0x%x\n", regs->sd_emmc_start);
    AML_SD_EMMC_TRACE("sd_emmc_cfg : 0x%x\n", regs->sd_emmc_cfg);
    aml_sd_emmc_dump_cfg(regs->sd_emmc_cfg);
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

uint32_t get_clk_freq(uint32_t clk_src) {
    if (clk_src == AML_SD_EMMC_FCLK_DIV2_SRC) {
        return AML_SD_EMMC_FCLK_DIV2_FREQ;
    }
    return AML_SD_EMMC_CTS_OSCIN_CLK_FREQ;
}

static void aml_sd_emmc_release(void* ctx) {
    aml_sd_emmc_t* dev = ctx;
    if (dev->irq_handle != ZX_HANDLE_INVALID)
        zx_interrupt_destroy(dev->irq_handle);
    if (dev->irq_thread)
        thrd_join(dev->irq_thread, NULL);
    io_buffer_release(&dev->mmio);
    io_buffer_release(&dev->descs_buffer);
    zx_handle_close(dev->irq_handle);
    zx_handle_close(dev->bti);
    free(dev);
}

static zx_status_t aml_sd_emmc_host_info(void* ctx, sdmmc_host_info_t* info) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    mtx_lock(&dev->mtx);
    memcpy(info, &dev->info, sizeof(dev->info));
    mtx_unlock(&dev->mtx);
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_bus_width(void* ctx, uint32_t bw) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;

    mtx_lock(&dev->mtx);
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
        mtx_unlock(&dev->mtx);
        return ZX_ERR_OUT_OF_RANGE;
    }

    regs->sd_emmc_cfg = config;
    mtx_unlock(&dev->mtx);
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_do_tuning_transfer(aml_sd_emmc_t *dev, uint8_t *tuning_res,
                                                  size_t blk_pattern_size){
    sdmmc_req_t tuning_req = {
        .cmd_idx = MMC_SEND_TUNING_BLOCK,
        .cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS,
        .arg = 0,
        .blockcount = 1,
        .blocksize = blk_pattern_size,
        .use_dma = false,
        .virt = tuning_res,
    };
    return aml_sd_emmc_request(dev, &tuning_req);
}

static bool aml_sd_emmc_tuning_test_delay(aml_sd_emmc_t *dev, const uint8_t *blk_pattern,
                                          size_t blk_pattern_size, uint32_t adj_delay) {
    mtx_lock(&dev->mtx);
    aml_sd_emmc_regs_t* regs = dev->regs;
    uint32_t adjust_reg = regs->sd_emmc_adjust;
    update_bits(&adjust_reg, AML_SD_EMMC_ADJUST_ADJ_DELAY_MASK,
                AML_SD_EMMC_ADJUST_ADJ_DELAY_LOC, adj_delay);
    adjust_reg |= AML_SD_EMMC_ADJUST_ADJ_FIXED;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_RISE;
    adjust_reg &= ~AML_SD_EMMC_ADJUST_CALI_ENABLE;
    regs->sd_emmc_adjust = adjust_reg;
    mtx_unlock(&dev->mtx);

    zx_status_t status = ZX_OK;
    size_t n;
    for (n = 0; n < AML_SD_EMMC_ADJ_DELAY_TEST_ATTEMPTS; n++) {
        uint8_t tuning_res[512] = {0};
        status = aml_sd_emmc_do_tuning_transfer(dev, tuning_res, blk_pattern_size);
        if (status != ZX_OK || memcmp(blk_pattern, tuning_res, blk_pattern_size)) {
            break;
        }
    }
    return (n == AML_SD_EMMC_ADJ_DELAY_TEST_ATTEMPTS);
}

static zx_status_t aml_sd_emmc_tuning_calculate_best_window(aml_sd_emmc_t *dev,
                                                            const uint8_t *tuning_blk,
                                                            size_t tuning_blk_size,
                                                            uint32_t cur_clk_div, int *best_start,
                                                            uint32_t *best_size) {
    int cur_win_start = -1, best_win_start = -1;
    uint32_t cycle_begin_win_size = 0, cur_win_size = 0, best_win_size = 0;

    for (uint32_t adj_delay = 0; adj_delay < cur_clk_div; adj_delay++) {
        if (aml_sd_emmc_tuning_test_delay(dev, tuning_blk, tuning_blk_size, adj_delay)) {
            if (cur_win_start < 0) {
                cur_win_start = adj_delay;
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

static zx_status_t aml_sd_emmc_perform_tuning(void* ctx) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    mtx_lock(&dev->mtx);

    aml_sd_emmc_regs_t* regs = dev->regs;
    const uint8_t *tuning_blk;
    size_t tuning_blk_size;
    int best_win_start = -1;
    uint32_t best_win_size = 0;
    uint32_t tries = 0;

    uint32_t config = regs->sd_emmc_cfg;
    uint32_t bw = get_bits(config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC);
    if (bw == AML_SD_EMMC_CFG_BUS_WIDTH_4BIT) {
        tuning_blk = aml_sd_emmc_tuning_blk_pattern_4bit;
        tuning_blk_size = sizeof(aml_sd_emmc_tuning_blk_pattern_4bit);
    } else if (bw == AML_SD_EMMC_CFG_BUS_WIDTH_8BIT) {
        tuning_blk = aml_sd_emmc_tuning_blk_pattern_8bit;
        tuning_blk_size = sizeof(aml_sd_emmc_tuning_blk_pattern_8bit);
    } else {
        zxlogf(ERROR, "aml_sd_emmc_perform_tuning: Tuning at wrong buswidth: %d\n", bw);
        mtx_unlock(&dev->mtx);
        return ZX_ERR_INTERNAL;
    }

    uint32_t clk_val, clk_div;
    clk_val = regs->sd_emmc_clock;
    clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC);
    mtx_unlock(&dev->mtx);

    do {
        aml_sd_emmc_tuning_calculate_best_window(dev, tuning_blk, tuning_blk_size,
                                                 clk_div, &best_win_start, &best_win_size);
        if (best_win_size == 0) {
            // Lower the frequency and try again
            zxlogf(TRACE, "Tuning failed. Reducing the frequency and trying again\n");
            mtx_lock(&dev->mtx);
            clk_val = regs->sd_emmc_clock;
            clk_div = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK,
                               AML_SD_EMMC_CLOCK_CFG_DIV_LOC);
            clk_div += 2;
            if (clk_div > (AML_SD_EMMC_CLOCK_CFG_DIV_MASK >> AML_SD_EMMC_CLOCK_CFG_DIV_LOC)) {
                clk_div = AML_SD_EMMC_CLOCK_CFG_DIV_MASK >> AML_SD_EMMC_CLOCK_CFG_DIV_LOC;
            }
            update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                        clk_div);
            regs->sd_emmc_clock = clk_val;
            uint32_t clk_src = get_bits(clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK,
                                        AML_SD_EMMC_CLOCK_CFG_SRC_LOC);
            uint32_t cur_freq = (get_clk_freq(clk_src)) / clk_div;
            if (dev->max_freq > cur_freq) {
                // Update max freq accordingly
                dev->max_freq = cur_freq;
            }
            mtx_unlock(&dev->mtx);
        }
    } while (best_win_size == 0 && ++tries < AML_SD_EMMC_MAX_TUNING_TRIES);

    if (best_win_size == 0) {
        zxlogf(ERROR, "aml_sd_emmc_perform_tuning: Tuning failed\n");
        return ZX_ERR_IO;
    }

    mtx_lock(&dev->mtx);
    uint32_t best_adj_delay = 0;
    uint32_t adjust_reg = regs->sd_emmc_adjust;

    clk_val = regs->sd_emmc_clock;
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
    regs->sd_emmc_adjust = adjust_reg;

    mtx_unlock(&dev->mtx);
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_bus_freq(void* ctx, uint32_t freq) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;

    mtx_lock(&dev->mtx);
    aml_sd_emmc_regs_t* regs = dev->regs;
    uint32_t clk = 0, clk_src = 0, clk_div = 0;
    uint32_t clk_val = regs->sd_emmc_clock;

    if (freq == 0) {
        //TODO: Disable clock here
    } else if (freq > dev->max_freq) {
        freq = dev->max_freq;
    } else if (freq < dev->min_freq) {
        freq = dev->min_freq;
    }
    if (freq < AML_SD_EMMC_FCLK_DIV2_MIN_FREQ) {
        clk_src = AML_SD_EMMC_CTS_OSCIN_CLK_SRC;
        clk = AML_SD_EMMC_CTS_OSCIN_CLK_FREQ;
    } else {
        clk_src = AML_SD_EMMC_FCLK_DIV2_SRC;
        clk = AML_SD_EMMC_FCLK_DIV2_FREQ;
    }
    clk_div = clk/freq;
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC, clk_div);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC, clk_src);
    regs->sd_emmc_clock = clk_val;

    mtx_unlock(&dev->mtx);
    return ZX_OK;
}

static void aml_sd_emmc_init_regs(aml_sd_emmc_t* dev) {
    aml_sd_emmc_regs_t* regs = dev->regs;
    uint32_t config = 0;
    uint32_t clk_val = 0;
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC, AML_SD_EMMC_DEFAULT_CLK_CORE_PHASE);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC,
                AML_SD_EMMC_DEFAULT_CLK_SRC);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                AML_SD_EMMC_DEFAULT_CLK_DIV);
    clk_val |= AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON;
    regs->sd_emmc_clock = clk_val;

    update_bits(&config, AML_SD_EMMC_CFG_BL_LEN_MASK, AML_SD_EMMC_CFG_BL_LEN_LOC,
                AML_SD_EMMC_DEFAULT_BL_LEN);
    update_bits(&config, AML_SD_EMMC_CFG_RESP_TIMEOUT_MASK, AML_SD_EMMC_CFG_RESP_TIMEOUT_LOC,
                AML_SD_EMMC_DEFAULT_RESP_TIMEOUT);
    update_bits(&config, AML_SD_EMMC_CFG_RC_CC_MASK, AML_SD_EMMC_CFG_RC_CC_LOC,
                AML_SD_EMMC_DEFAULT_RC_CC);
    update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                 AML_SD_EMMC_CFG_BUS_WIDTH_1BIT);

    regs->sd_emmc_cfg = config;
    regs->sd_emmc_status = AML_SD_EMMC_IRQ_ALL_CLEAR;
    regs->sd_emmc_irq_en = AML_SD_EMMC_IRQ_ALL_CLEAR;
}

static void aml_sd_emmc_hw_reset(void* ctx) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    mtx_lock(&dev->mtx);
    if (dev->gpio_count == 1) {
        //Currently we only have 1 gpio
        gpio_config(&dev->gpio, 0, GPIO_DIR_OUT);
        gpio_write(&dev->gpio, 0, 0);
        usleep(10 * 1000);
        gpio_write(&dev->gpio, 0, 1);
        usleep(10 * 1000);
    }
    aml_sd_emmc_init_regs(dev);
    mtx_unlock(&dev->mtx);
}

static zx_status_t aml_sd_emmc_set_bus_timing(void* ctx, sdmmc_timing_t timing) {
    aml_sd_emmc_t* dev = ctx;

    mtx_lock(&dev->mtx);
    aml_sd_emmc_regs_t* regs = dev->regs;
    uint32_t config = regs->sd_emmc_cfg;
    uint32_t clk_val = regs->sd_emmc_clock;

    if (timing == SDMMC_TIMING_HS400 || timing == SDMMC_TIMING_HSDDR) {
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

    regs->sd_emmc_cfg = config;
    regs->sd_emmc_clock = clk_val;
    mtx_unlock(&dev->mtx);
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_set_signal_voltage(void* ctx, sdmmc_voltage_t voltage) {
    //Amlogic controller does not allow to modify voltage
    //We do not return an error here since things work fine without switching the voltage.
    return ZX_OK;
}

static int aml_sd_emmc_irq_thread(void *ctx) {
    aml_sd_emmc_t* dev = ctx;
    uint32_t status_irq;

    while (1) {
        zx_status_t status = ZX_OK;
        status = zx_interrupt_wait(dev->irq_handle, NULL);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_sd_emmc_irq_thread: zx_interrupt_wait got %d\n", status);
            break;
        }
        mtx_lock(&dev->mtx);
        aml_sd_emmc_regs_t* regs = dev->regs;
        sdmmc_req_t *req = dev->cur_req;

        if (req == NULL) {
            status = ZX_ERR_IO_INVALID;
            zxlogf(ERROR, "aml_sd_emmc_irq_thread: Got a spurious interrupt\n");
            //TODO(ravoorir): Do some error recovery here and continue instead
            // of breaking.
            mtx_unlock(&dev->mtx);
            break;
        }

        status_irq = regs->sd_emmc_status;
        if (!(status_irq & AML_SD_EMMC_STATUS_END_OF_CHAIN)) {
            status = ZX_ERR_IO_INVALID;
            goto complete;
        }

        uint32_t rxd_err = get_bits(status_irq, AML_SD_EMMC_STATUS_RXD_ERR_MASK,
                                    AML_SD_EMMC_STATUS_RXD_ERR_LOC);
        if (rxd_err) {
            AML_SD_EMMC_ERROR("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n", req->cmd_idx,
                              status_irq, rxd_err);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_TXD_ERR) {
            AML_SD_EMMC_ERROR("TX Data CRC Error, cmd%d, status=0x%x TXD_ERR\n", req->cmd_idx,
                              status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_ERR) {
            AML_SD_EMMC_ERROR("Controller does not own the descriptor, cmd%d, status=0x%x\n",
                              req->cmd_idx, status_irq);
            status = ZX_ERR_IO_INVALID;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_ERR) {
            AML_SD_EMMC_ERROR("Response CRC Error, cmd%d, status=0x%x\n", req->cmd_idx, status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_TIMEOUT) {
            AML_SD_EMMC_ERROR("No response reived before time limit, cmd%d, status=0x%x\n",
                    req->cmd_idx, status_irq);
            status = ZX_ERR_TIMED_OUT;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_TIMEOUT) {
            AML_SD_EMMC_ERROR("Descriptor execution timed out, cmd%d, status=0x%x\n", req->cmd_idx,
                        status_irq);
            status = ZX_ERR_TIMED_OUT;
            goto complete;
        }

        if (req->cmd_flags & SDMMC_RESP_LEN_136) {
            req->response[0] = regs->sd_emmc_cmd_rsp;
            req->response[1] = regs->sd_emmc_cmd_rsp1;
            req->response[2] = regs->sd_emmc_cmd_rsp2;
            req->response[3] = regs->sd_emmc_cmd_rsp3;
        } else {
            req->response[0] = regs->sd_emmc_cmd_rsp;
        }
        if ((!req->use_dma) && (req->cmd_flags & SDMMC_CMD_READ)) {
            volatile uint64_t *dest = (uint64_t *)req->virt;
            uint32_t length = req->blockcount * req->blocksize;
            volatile uint64_t *end = (uint64_t *)(req->virt + length);
            volatile uint64_t *src = (uint64_t *)(io_buffer_virt(&dev->mmio) +
                                                  AML_SD_EMMC_PING_BUFFER_BASE);
            while (dest < end) {
                *dest++ = *src++;
            }
        }

complete:
        req->status = status;
        regs->sd_emmc_status = AML_SD_EMMC_IRQ_ALL_CLEAR;
        dev->cur_req = NULL;
        completion_signal(&dev->req_completion);
        mtx_unlock(&dev->mtx);
    }
    return 0;
}

static void aml_sd_emmc_setup_cmd_desc(aml_sd_emmc_t *dev, sdmmc_req_t* req,
                                       aml_sd_emmc_desc_t **out_desc) {
    aml_sd_emmc_desc_t *desc;
    if (req->use_dma) {
        ZX_DEBUG_ASSERT((dev->info.caps & SDMMC_HOST_CAP_ADMA2));
        desc = (aml_sd_emmc_desc_t *)io_buffer_virt(&dev->descs_buffer);
        memset(desc, 0, dev->descs_buffer.size);
    } else {
        desc = (aml_sd_emmc_desc_t *)(io_buffer_virt(&dev->mmio) + AML_SD_EMMC_SRAM_MEMORY_BASE);
    }
    uint32_t cmd_info = 0;
    if (req->cmd_flags == 0) {
        cmd_info |= AML_SD_EMMC_CMD_INFO_NO_RESP;
    } else {
        if (req->cmd_flags & SDMMC_RESP_LEN_136) {
            cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_128;
        }

        if (!(req->cmd_flags & SDMMC_RESP_CRC_CHECK)){
            cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_NO_CRC;
        }

        if (req->cmd_flags & SDMMC_RESP_LEN_48B) {
            cmd_info |= AML_SD_EMMC_CMD_INFO_R1B;
        }

        cmd_info |= AML_SD_EMMC_CMD_INFO_RESP_NUM;
    }
    update_bits(&cmd_info, AML_SD_EMMC_CMD_INFO_CMD_IDX_MASK, AML_SD_EMMC_CMD_INFO_CMD_IDX_LOC,
                AML_SD_EMMC_COMMAND(req->cmd_idx));
    cmd_info &= ~AML_SD_EMMC_CMD_INFO_ERROR;
    cmd_info |= AML_SD_EMMC_CMD_INFO_OWNER;
    cmd_info &= ~AML_SD_EMMC_CMD_INFO_END_OF_CHAIN;
    desc->cmd_info = cmd_info;
    desc->cmd_arg = req->arg;
    desc->data_addr = 0;
    desc->resp_addr = 0;
    *out_desc = desc;
}

static zx_status_t aml_sd_emmc_setup_data_descs_dma(aml_sd_emmc_t *dev, sdmmc_req_t *req,
                                                             aml_sd_emmc_desc_t *cur_desc,
                                                             aml_sd_emmc_desc_t **last_desc) {
    block_op_t* bop = &req->txn->bop;
    uint64_t pagecount = ((bop->rw.offset_vmo & PAGE_MASK) + bop->rw.length + PAGE_MASK) /
                          PAGE_SIZE;
    if (pagecount > SDMMC_PAGES_COUNT) {
        zxlogf(ERROR, "aml-sd-emmc.c: too many pages %lu vs %lu\n", pagecount, SDMMC_PAGES_COUNT);
        return ZX_ERR_INVALID_ARGS;
    }

    // pin the vmo
    zx_paddr_t phys[SDMMC_PAGES_COUNT];
    zx_handle_t pmt;
    // offset_vmo is converted to bytes by the sdmmc layer
    uint32_t options = bop->command == BLOCK_OP_READ ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
    zx_status_t st = zx_bti_pin(dev->bti, options, bop->rw.vmo,
                                bop->rw.offset_vmo & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, phys, pagecount, &pmt);
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml-sd-emmc: bti-pin failed with error %d\n", st);
        return st;
    }
    if (req->cmd_flags & SDMMC_CMD_READ) {
        st = zx_vmo_op_range(bop->rw.vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                             bop->rw.offset_vmo, bop->rw.length, NULL, 0);
    } else {
        st = zx_vmo_op_range(bop->rw.vmo, ZX_VMO_OP_CACHE_CLEAN,
                             bop->rw.offset_vmo, bop->rw.length, NULL, 0);
    }
    if (st != ZX_OK) {
        zxlogf(ERROR, "aml-sd-emmc: cache clean failed with error  %d\n", st);
        return st;
    }

    // cache this for zx_pmt_unpin() later
    req->pmt = pmt;

    phys_iter_buffer_t buf = {
        .phys = phys,
        .phys_count = pagecount,
        .length = bop->rw.length,
        .vmo_offset = bop->rw.offset_vmo,
    };
    phys_iter_t iter;
    phys_iter_init(&iter, &buf, PAGE_SIZE);

    int count = 0;
    size_t length;
    zx_paddr_t paddr;
    uint32_t blockcount;
    aml_sd_emmc_desc_t *desc = cur_desc;
    for (;;) {
        length = phys_iter_next(&iter, &paddr);
        if (length == 0) {
            if (desc != io_buffer_virt(&dev->descs_buffer)) {
                desc -= 1;
                *last_desc = desc;
                break;
            } else {
                zxlogf(TRACE, "aml-sd-emmc: empty descriptor list!\n");
                return ZX_ERR_NOT_SUPPORTED;
            }
        } else if (length > PAGE_SIZE) {
            zxlogf(TRACE, "aml-sd-emmc: chunk size > %zu is unsupported\n", length);
            return ZX_ERR_NOT_SUPPORTED;
        } else if ((++count) > AML_DMA_DESC_MAX_COUNT) {
            zxlogf(TRACE, "aml-sd-emmc: request with more than %d chunks is unsupported\n",
                    AML_DMA_DESC_MAX_COUNT);
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
        desc->cmd_info &= ~AML_SD_EMMC_CMD_INFO_ERROR;

        uint32_t blocksize = req->blocksize;
        blockcount = length/blocksize;
        ZX_DEBUG_ASSERT(((length % blocksize) == 0));

        if (blockcount > 1) {
            desc->cmd_info |= AML_SD_EMMC_CMD_INFO_BLOCK_MODE;
            update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                        AML_SD_EMMC_CMD_INFO_LEN_LOC, blockcount);
        } else {
            update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK,
                        AML_SD_EMMC_CMD_INFO_LEN_LOC, req->blocksize);
        }

        desc->data_addr = (uint32_t)paddr;
        desc += 1;
    }
    return ZX_OK;
}

static zx_status_t aml_sd_emmc_setup_data_descs_pio(aml_sd_emmc_t *dev, sdmmc_req_t *req,
                                                         aml_sd_emmc_desc_t *desc,
                                                         aml_sd_emmc_desc_t **last_desc) {
    zx_status_t status = ZX_OK;
    uint32_t length = req->blockcount * req->blocksize;

    if (length > AML_SD_EMMC_MAX_PIO_DATA_SIZE) {
        zxlogf(ERROR, "Request transfer size is greater than max transfer size\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_IO;
    if (!(req->cmd_flags & SDMMC_CMD_READ)) {
        desc->cmd_info |= AML_SD_EMMC_CMD_INFO_DATA_WR;
        volatile uint64_t *src = (uint64_t *)req->virt;
        volatile uint64_t *end = (uint64_t *)(req->virt + length);
        volatile uint64_t *dest = (uint64_t *)(io_buffer_virt(&dev->mmio) +
                                               AML_SD_EMMC_PING_BUFFER_BASE);
        while (src < end) {
            *dest++ = *src++;
        }
        io_buffer_cache_flush(&dev->mmio, AML_SD_EMMC_PING_BUFFER_BASE, length);
    } else if (req->cmd_flags & SDMMC_CMD_READ) {
        io_buffer_cache_flush_invalidate(&dev->mmio, AML_SD_EMMC_PING_BUFFER_BASE, length);
    }

    // Make it 1 block
    update_bits(&desc->cmd_info, AML_SD_EMMC_CMD_INFO_LEN_MASK, AML_SD_EMMC_CMD_INFO_LEN_LOC,
                length);
    // data_addr[0] = 0 for DDR. data_addr[0] = 1 if address is from SRAM
    zx_paddr_t buffer_phys = io_buffer_phys(&dev->mmio) + AML_SD_EMMC_PING_BUFFER_BASE;
    desc->data_addr = (uint32_t)buffer_phys | 1;
    *last_desc = desc;
    return status;
}

static zx_status_t aml_sd_emmc_setup_data_descs(aml_sd_emmc_t *dev, sdmmc_req_t *req,
                                                aml_sd_emmc_desc_t *desc,
                                                aml_sd_emmc_desc_t **last_desc) {
    if (req->use_dma) {
        return aml_sd_emmc_setup_data_descs_dma(dev, req, desc, last_desc);
    }

    // Data is at address req->virt
    return aml_sd_emmc_setup_data_descs_pio(dev, req, desc, last_desc);
}

static zx_status_t aml_sd_emmc_finish_req(aml_sd_emmc_t* dev, sdmmc_req_t* req) {
    zx_status_t st = ZX_OK;
    if (req->use_dma && req->pmt != ZX_HANDLE_INVALID) {
        /*
         * Clean the cache one more time after the DMA operation because there
         * might be a possibility of cpu prefetching while the DMA operation is
         * going on.
         */
        block_op_t* bop = &req->txn->bop;
        if ((req->cmd_flags & SDMMC_CMD_READ) && req->use_dma) {
            st = zx_vmo_op_range(bop->rw.vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                       bop->rw.offset_vmo, bop->rw.length, NULL, 0);
            if (st != ZX_OK) {
                zxlogf(ERROR, "aml-sd-emmc: cache clean failed with error  %d\n", st);
            }
        }

        st = zx_pmt_unpin(req->pmt);
        if (st != ZX_OK) {
            zxlogf(ERROR, "aml-sd-emmc: error %d in pmt_unpin\n", st);
        }
        req->pmt = ZX_HANDLE_INVALID;
    }
    return st;
}

zx_status_t aml_sd_emmc_request(void *ctx, sdmmc_req_t* req) {
    aml_sd_emmc_t *dev = (aml_sd_emmc_t *)ctx;
    zx_status_t status = ZX_OK;

    mtx_lock(&dev->mtx);
    aml_sd_emmc_regs_t* regs = dev->regs;

    // stop executing
    uint32_t start_reg = regs->sd_emmc_start;
    start_reg &= ~AML_SD_EMMC_START_DESC_BUSY;
    regs->sd_emmc_start = start_reg;
    aml_sd_emmc_desc_t* desc, *last_desc;

    aml_sd_emmc_setup_cmd_desc(dev, req, &desc);
    last_desc = desc;
    if (req->cmd_flags & SDMMC_RESP_DATA_PRESENT) {
        status = aml_sd_emmc_setup_data_descs(dev, req, desc, &last_desc);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_sd_emmc_request: Failed to setup data descriptors\n");
            mtx_unlock(&dev->mtx);
            return status;
        }
    }

    last_desc->cmd_info |= AML_SD_EMMC_CMD_INFO_END_OF_CHAIN;
    AML_SD_EMMC_TRACE("SUBMIT req:%p cmd_idx: %d cmd_cfg: 0x%x cmd_dat: 0x%x cmd_arg: 0x%x\n", req,
                      req->cmd_idx, desc->cmd_info, desc->data_addr, desc->cmd_arg);

    dev->cur_req = req;
    zx_paddr_t desc_phys;

    start_reg = regs->sd_emmc_start;
    if (req->use_dma) {
        desc_phys = io_buffer_phys(&dev->descs_buffer);
        io_buffer_cache_flush(&dev->descs_buffer, 0,
                               AML_DMA_DESC_MAX_COUNT * sizeof(aml_sd_emmc_desc_t));
        //Read desc from external DDR
        start_reg &= ~AML_SD_EMMC_START_DESC_INT;
    } else {
        io_buffer_physmap(&dev->mmio);
        desc_phys = (io_buffer_phys(&dev->mmio)) + AML_SD_EMMC_SRAM_MEMORY_BASE;
        start_reg |= AML_SD_EMMC_START_DESC_INT;
    }

    start_reg |= AML_SD_EMMC_START_DESC_BUSY;
    update_bits(&start_reg, AML_SD_EMMC_START_DESC_ADDR_MASK, AML_SD_EMMC_START_DESC_ADDR_LOC,
              (((uint32_t)desc_phys) >> 2));
    mtx_unlock(&dev->mtx);
    regs->sd_emmc_start = start_reg;

    completion_wait(&dev->req_completion, ZX_TIME_INFINITE);
    aml_sd_emmc_finish_req(dev, req);
    completion_reset(&dev->req_completion);
    return req->status;
}

static zx_protocol_device_t aml_sd_emmc_device_proto = {
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
    dev->req_completion = COMPLETION_INIT;

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

    status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sdhci_bind: pdev_map_interrupt failed %d\n", status);
        goto fail;
    }

    int rc = thrd_create_with_name(&dev->irq_thread, aml_sd_emmc_irq_thread, dev,
                                   "aml_sd_emmc_irq_thread");
    if (rc != thrd_success) {
        zx_handle_close(dev->irq_handle);
        dev->irq_handle = ZX_HANDLE_INVALID;
        status = thrd_status_to_zx_status(rc);
        goto fail;
    }

    dev->info.caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330;
    // Populate board specific information
    aml_sd_emmc_config_t dev_config;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_DRIVER_DATA,
                                 &dev_config, sizeof(aml_sd_emmc_config_t), &actual);
    if (status != ZX_OK || actual != sizeof(aml_sd_emmc_config_t)) {
        zxlogf(ERROR, "aml_sd_emmc_bind: device_get_metadata failed\n");
        goto fail;
    }
    if (dev_config.supports_dma) {
        dev->info.caps |= SDMMC_HOST_CAP_ADMA2;
    }

    dev->regs = (aml_sd_emmc_regs_t*)io_buffer_virt(&dev->mmio);

    if (dev->info.caps & SDMMC_HOST_CAP_ADMA2) {
        status = io_buffer_init(&dev->descs_buffer, dev->bti,
                                AML_DMA_DESC_MAX_COUNT * sizeof(aml_sd_emmc_desc_t),
                                IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_sd_emmc_bind: Failed to allocate dma descriptors\n");
            goto fail;
        }
        dev->info.max_transfer_size = AML_DMA_DESC_MAX_COUNT * PAGE_SIZE;
    } else {
        dev->info.max_transfer_size = AML_SD_EMMC_MAX_PIO_DATA_SIZE;
    }

    dev->max_freq = AML_SD_EMMC_MAX_FREQ;
    dev->min_freq = AML_SD_EMMC_MIN_FREQ;
    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-sd-emmc",
        .ctx = dev,
        .ops = &aml_sd_emmc_device_proto,
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
