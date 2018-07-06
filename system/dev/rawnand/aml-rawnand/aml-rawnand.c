// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/rawnand.h>
#include <hw/reg.h>

#include <sync/completion.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <string.h>

#include "onfi.h"
#include <soc/aml-common/aml-rawnand.h>
#include "aml-rawnand.h"

static const uint32_t chipsel[2] = {NAND_CE0, NAND_CE1};

struct aml_controller_params aml_params = {
    8,
    2,
    /* The 2 following values are overwritten by page0 contents */
    1,                /* rand-mode is 1 for page0 */
    AML_ECC_BCH60_1K, /* This is the BCH setting for page0 */
};

static void aml_cmd_ctrl(void* ctx,
                         int32_t cmd, uint32_t ctrl);
static uint8_t aml_read_byte(void* ctx);
static zx_status_t aml_nand_init(aml_raw_nand_t* raw_nand);

static const char* aml_ecc_string(uint32_t ecc_mode) {
    const char* s;

    switch (ecc_mode) {
    case AML_ECC_BCH8:
        s = "AML_ECC_BCH8";
        break;
    case AML_ECC_BCH8_1K:
        s = "AML_ECC_BCH8_1K";
        break;
    case AML_ECC_BCH24_1K:
        s = "AML_ECC_BCH24_1K";
        break;
    case AML_ECC_BCH30_1K:
        s = "AML_ECC_BCH30_1K";
        break;
    case AML_ECC_BCH40_1K:
        s = "AML_ECC_BCH40_1K";
        break;
    case AML_ECC_BCH50_1K:
        s = "AML_ECC_BCH50_1K";
        break;
    case AML_ECC_BCH60_1K:
        s = "AML_ECC_BCH60_1K";
        break;
    default:
        s = "BAD ECC Algorithm";
        break;
    }
    return s;
}

uint32_t aml_get_ecc_pagesize(aml_raw_nand_t* raw_nand, uint32_t ecc_mode) {
    uint32_t ecc_page;

    switch (ecc_mode) {
    case AML_ECC_BCH8:
        ecc_page = 512;
        break;
    case AML_ECC_BCH8_1K:
    case AML_ECC_BCH24_1K:
    case AML_ECC_BCH30_1K:
    case AML_ECC_BCH40_1K:
    case AML_ECC_BCH50_1K:
    case AML_ECC_BCH60_1K:
        ecc_page = 1024;
        break;
    default:
        ecc_page = 0;
        break;
    }
    return ecc_page;
}

static void aml_cmd_idle(aml_raw_nand_t* raw_nand, uint32_t time) {
    uint32_t cmd = 0;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cmd = raw_nand->chip_select | AML_CMD_IDLE | (time & 0x3ff);
    writel(cmd, reg + P_NAND_CMD);
}

static zx_status_t aml_wait_cmd_finish(aml_raw_nand_t* raw_nand,
                                       unsigned int timeout_ms) {
    uint32_t cmd_size = 0;
    zx_status_t ret = ZX_OK;
    uint64_t total_time = 0;
    uint32_t numcmds;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    /* wait until cmd fifo is empty */
    while (true) {
        cmd_size = readl(reg + P_NAND_CMD);
        numcmds = (cmd_size >> 22) & 0x1f;
        if (numcmds == 0)
            break;
        usleep(10);
        total_time += 10;
        if (total_time > (timeout_ms * 1000)) {
            ret = ZX_ERR_TIMED_OUT;
            break;
        }
    }
    if (ret == ZX_ERR_TIMED_OUT)
        zxlogf(ERROR, "wait for empty cmd FIFO time out\n");
    return ret;
}

static void aml_cmd_seed(aml_raw_nand_t* raw_nand, uint32_t seed) {
    uint32_t cmd;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cmd = AML_CMD_SEED | (0xc2 + (seed & 0x7fff));
    writel(cmd, reg + P_NAND_CMD);
}

static void aml_cmd_n2m(aml_raw_nand_t* raw_nand, uint32_t ecc_pages,
                        uint32_t ecc_pagesize) {
    uint32_t cmd;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cmd = CMDRWGEN(AML_CMD_N2M,
                   raw_nand->controller_params.rand_mode,
                   raw_nand->controller_params.bch_mode,
                   0,
                   ecc_pagesize,
                   ecc_pages);
    writel(cmd, reg + P_NAND_CMD);
}

static void aml_cmd_m2n_page0(aml_raw_nand_t* raw_nand) {
    /* TODO */
}

static void aml_cmd_m2n(aml_raw_nand_t* raw_nand, uint32_t ecc_pages,
                        uint32_t ecc_pagesize) {
    uint32_t cmd;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cmd = CMDRWGEN(AML_CMD_M2N,
                   raw_nand->controller_params.rand_mode,
                   raw_nand->controller_params.bch_mode,
                   0, ecc_pagesize,
                   ecc_pages);
    writel(cmd, reg + P_NAND_CMD);
}

static void aml_cmd_n2m_page0(aml_raw_nand_t* raw_nand) {
    uint32_t cmd;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    /*
     * For page0 reads, we must use AML_ECC_BCH60_1K,
     * and rand-mode == 1.
     */
    cmd = CMDRWGEN(AML_CMD_N2M,
                   1,                /* force rand_mode */
                   AML_ECC_BCH60_1K, /* force bch_mode  */
                   1,                /* shortm == 1     */
                   384 >> 3,
                   1);
    writel(cmd, reg + P_NAND_CMD);
}

static zx_status_t aml_wait_dma_finish(aml_raw_nand_t* raw_nand) {
    aml_cmd_idle(raw_nand, 0);
    aml_cmd_idle(raw_nand, 0);
    return aml_wait_cmd_finish(raw_nand, DMA_BUSY_TIMEOUT);
}

/*
 * Return the aml_info_format struct corresponding to the i'th
 * ECC page. THIS ASSUMES user_mode == 2 (2 OOB bytes per ECC page).
 */
static struct aml_info_format* aml_info_ptr(aml_raw_nand_t* raw_nand,
                                            int i) {
    struct aml_info_format* p;

    p = (struct aml_info_format*)raw_nand->info_buf;
    return &p[i];
}

/*
 * In the case where user_mode == 2, info_buf contains one nfc_info_format
 * struct per ECC page on completion of a read. This 8 byte structure has
 * the 2 OOB bytes and ECC/error status
 */
static zx_status_t aml_get_oob_byte(aml_raw_nand_t* raw_nand,
                                    uint8_t* oob_buf) {
    struct aml_info_format* info;
    int count = 0;
    uint32_t ecc_pagesize, ecc_pages;

    ecc_pagesize = aml_get_ecc_pagesize(raw_nand,
                                        raw_nand->controller_params.bch_mode);
    ecc_pages = raw_nand->writesize / ecc_pagesize;
    /*
     * user_mode is 2 in our case - 2 bytes of OOB for every
     * ECC page.
     */
    if (raw_nand->controller_params.user_mode != 2)
        return ZX_ERR_NOT_SUPPORTED;
    for (uint32_t i = 0;
         i < ecc_pages;
         i++) {
        info = aml_info_ptr(raw_nand, i);
        oob_buf[count++] = info->info_bytes & 0xff;
        oob_buf[count++] = (info->info_bytes >> 8) & 0xff;
    }
    return ZX_OK;
}

static zx_status_t aml_set_oob_byte(aml_raw_nand_t* raw_nand,
                                    const uint8_t* oob_buf,
                                    uint32_t ecc_pages)
{
    struct aml_info_format* info;
    int count = 0;

    /*
     * user_mode is 2 in our case - 2 bytes of OOB for every
     * ECC page.
     */
    if (raw_nand->controller_params.user_mode != 2)
        return ZX_ERR_NOT_SUPPORTED;
    for (uint32_t i = 0; i < ecc_pages; i++) {
        info = aml_info_ptr(raw_nand, i);
        info->info_bytes = oob_buf[count] | (oob_buf[count + 1] << 8);
        count += 2;
    }
    return ZX_OK;
}

/*
 * Returns the maximum bitflips corrected on this NAND page
 * (the maximum bitflips across all of the ECC pages in this page).
 */
static int aml_get_ecc_corrections(aml_raw_nand_t* raw_nand, int ecc_pages) {
    struct aml_info_format* info;
    int bitflips = 0;
    uint8_t zero_cnt;

    for (int i = 0; i < ecc_pages; i++) {
        info = aml_info_ptr(raw_nand, i);
        if (info->ecc.eccerr_cnt == AML_ECC_UNCORRECTABLE_CNT) {
            /*
             * Why are we checking for zero_cnt here ?
             * Per Amlogic HW architect, this is to deal with
             * blank NAND pages. The entire blank page is 0xff.
             * When read with scrambler, the page will be ECC
             * uncorrectable, but if the total of zeroes in this
             * page is less than a threshold, then we know this is
             * blank page.
             */
            zero_cnt = info->zero_cnt & AML_ECC_UNCORRECTABLE_CNT;
            if (raw_nand->controller_params.rand_mode &&
                (zero_cnt < raw_nand->controller_params.ecc_strength)) {
                zxlogf(ERROR, "%s: Returning ECC failure\n",
                       __func__);
                return ECC_CHECK_RETURN_FF;
            }
            raw_nand->stats.failed++;
            continue;
        }
        raw_nand->stats.ecc_corrected += info->ecc.eccerr_cnt;
        bitflips = MAX(bitflips, info->ecc.eccerr_cnt);
    }
    return bitflips;
}

static zx_status_t aml_check_ecc_pages(aml_raw_nand_t* raw_nand, int ecc_pages) {
    struct aml_info_format* info;

    for (int i = 0; i < ecc_pages; i++) {
        info = aml_info_ptr(raw_nand, i);
        if (info->ecc.completed == 0)
            return ZX_ERR_IO;
    }
    return ZX_OK;
}

static zx_status_t aml_queue_rb(aml_raw_nand_t* raw_nand) {
    uint32_t cmd, cfg;
    zx_status_t status;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    raw_nand->req_completion = COMPLETION_INIT;
    cfg = readl(reg + P_NAND_CFG);
    cfg |= (1 << 21);
    writel(cfg, reg + P_NAND_CFG);
    aml_cmd_idle(raw_nand, NAND_TWB_TIME_CYCLE);
    cmd = raw_nand->chip_select | AML_CMD_CLE | (NAND_CMD_STATUS & 0xff);
    writel(cmd, reg + P_NAND_CMD);
    aml_cmd_idle(raw_nand, NAND_TWB_TIME_CYCLE);
    cmd = AML_CMD_RB | AML_CMD_IO6 | (1 << 16) | (0x18 & 0x1f);
    writel(cmd, reg + P_NAND_CMD);
    aml_cmd_idle(raw_nand, 2);
    status = completion_wait(&raw_nand->req_completion,
                             ZX_SEC(1));
    if (status == ZX_ERR_TIMED_OUT) {
        zxlogf(ERROR, "%s: Request timed out, not woken up from irq\n",
               __func__);
    }
    return status;
}

static void aml_cmd_ctrl(void* ctx,
                         int32_t cmd, uint32_t ctrl) {
    aml_raw_nand_t* raw_nand = (aml_raw_nand_t*)ctx;

    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    if (cmd == NAND_CMD_NONE)
        return;
    if (ctrl & NAND_CLE)
        cmd = raw_nand->chip_select | AML_CMD_CLE | (cmd & 0xff);
    else
        cmd = raw_nand->chip_select | AML_CMD_ALE | (cmd & 0xff);
    writel(cmd, reg + P_NAND_CMD);
}

/* Read status byte */
static uint8_t aml_read_byte(void* ctx) {
    aml_raw_nand_t* raw_nand = (aml_raw_nand_t*)ctx;
    uint32_t cmd;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cmd = raw_nand->chip_select | AML_CMD_DRD | 0;
    nandctrl_send_cmd(raw_nand, cmd);

    aml_cmd_idle(raw_nand, NAND_TWB_TIME_CYCLE);

    aml_cmd_idle(raw_nand, 0);
    aml_cmd_idle(raw_nand, 0);
    aml_wait_cmd_finish(raw_nand,
                        CMD_FINISH_TIMEOUT_MS);
    return readb(reg + P_NAND_BUF);
}

static void aml_set_clock_rate(aml_raw_nand_t* raw_nand,
                               uint32_t clk_freq) {
    uint32_t always_on = 0x1 << 24;
    uint32_t clk;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[CLOCKREG_WINDOW]);

    /* For Amlogic type  AXG */
    always_on = 0x1 << 28;
    switch (clk_freq) {
    case 24:
        clk = 0x80000201;
        break;
    case 112:
        clk = 0x80000249;
        break;
    case 200:
        clk = 0x80000245;
        break;
    case 250:
        clk = 0x80000244;
        break;
    default:
        clk = 0x80000245;
        break;
    }
    clk |= always_on;
    writel(clk, reg);
}

static void aml_clock_init(aml_raw_nand_t* raw_nand) {
    uint32_t sys_clk_rate, bus_cycle, bus_timing;

    sys_clk_rate = 200;
    aml_set_clock_rate(raw_nand, sys_clk_rate);
    bus_cycle = 6;
    bus_timing = bus_cycle + 1;
    nandctrl_set_cfg(raw_nand, 0);
    nandctrl_set_timing_async(raw_nand, bus_timing, (bus_cycle - 1));
    nandctrl_send_cmd(raw_nand, 1 << 31);
}

static void aml_adjust_timings(aml_raw_nand_t* raw_nand,
                               uint32_t tRC_min, uint32_t tREA_max,
                               uint32_t RHOH_min) {
    int sys_clk_rate, bus_cycle, bus_timing;

    if (!tREA_max)
        tREA_max = TREA_MAX_DEFAULT;
    if (!RHOH_min)
        RHOH_min = RHOH_MIN_DEFAULT;
    if (tREA_max > 30)
        sys_clk_rate = 112;
    else if (tREA_max > 16)
        sys_clk_rate = 200;
    else
        sys_clk_rate = 250;
    aml_set_clock_rate(raw_nand, sys_clk_rate);
    bus_cycle = 6;
    bus_timing = bus_cycle + 1;
    nandctrl_set_cfg(raw_nand, 0);
    nandctrl_set_timing_async(raw_nand, bus_timing, (bus_cycle - 1));
    nandctrl_send_cmd(raw_nand, 1 << 31);
}

static bool is_page0_nand_page(uint32_t nand_page) {
    return ((nand_page <= AML_PAGE0_MAX_ADDR) &&
            ((nand_page % AML_PAGE0_STEP) == 0));
}

static zx_status_t aml_read_page_hwecc(void* ctx,
                                       void* data,
                                       void* oob,
                                       uint32_t nand_page,
                                       int* ecc_correct) {
    aml_raw_nand_t* raw_nand = (aml_raw_nand_t*)ctx;
    uint32_t cmd;
    zx_status_t status;
    uint64_t daddr = raw_nand->data_buf_paddr;
    uint64_t iaddr = raw_nand->info_buf_paddr;
    int ecc_c;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);
    uint32_t ecc_pagesize = 0; /* initialize to silence compiler */
    uint32_t ecc_pages;
    bool page0 = is_page0_nand_page(nand_page);

    if (!page0) {
        ecc_pagesize = aml_get_ecc_pagesize(raw_nand,
                                            raw_nand->controller_params.bch_mode);
        ecc_pages = raw_nand->writesize / ecc_pagesize;
        if (is_page0_nand_page(nand_page))
            return ZX_ERR_IO;
    } else
        ecc_pages = 1;
    /*
     * Flush and invalidate (only invalidate is really needed), the
     * info and data buffers before kicking off DMA into them.
     */
    io_buffer_cache_flush_invalidate(&raw_nand->data_buffer, 0,
                                     raw_nand->writesize);
    io_buffer_cache_flush_invalidate(&raw_nand->info_buffer, 0,
                                     ecc_pages * sizeof(struct aml_info_format));
    /* Send the page address into the controller */
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_READ0, 0x00,
                 nand_page, raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    cmd = GENCMDDADDRL(AML_CMD_ADL, daddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDDADDRH(AML_CMD_ADH, daddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDIADDRL(AML_CMD_AIL, iaddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDIADDRH(AML_CMD_AIH, iaddr);
    writel(cmd, reg + P_NAND_CMD);
    /* page0 needs randomization. so force it for page0 */
    if (page0 || raw_nand->controller_params.rand_mode)
        /*
         * Only need to set the seed if randomizing
         * is enabled.
         */
        aml_cmd_seed(raw_nand, nand_page);
    if (!page0)
        aml_cmd_n2m(raw_nand, ecc_pages, ecc_pagesize);
    else
        aml_cmd_n2m_page0(raw_nand);
    status = aml_wait_dma_finish(raw_nand);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: aml_wait_dma_finish failed %d\n",
               __func__, status);
        return status;
    }
    aml_queue_rb(raw_nand);
    status = aml_check_ecc_pages(raw_nand, ecc_pages);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: aml_check_ecc_pages failed %d\n",
               __func__, status);
        return status;
    }
    /*
     * Finally copy out the data and oob as needed
     */
    if (data != NULL) {
        if (!page0)
            memcpy(data, raw_nand->data_buf, raw_nand->writesize);
        else
            memcpy(data, raw_nand->data_buf, AML_PAGE0_LEN);
    }
    if (oob != NULL)
        status = aml_get_oob_byte(raw_nand, oob);
    ecc_c = aml_get_ecc_corrections(raw_nand, ecc_pages);
    if (ecc_c < 0) {
        zxlogf(ERROR, "%s: Uncorrectable ECC error on read\n",
               __func__);
        status = ZX_ERR_IO;
    }
    *ecc_correct = ecc_c;
    return status;
}

/*
 * TODO : Right now, the driver uses a buffer for DMA, which
 * is not needed. We should initiate DMA to/from pages passed in.
 */
static zx_status_t aml_write_page_hwecc(void* ctx,
                                        const void* data,
                                        const void* oob,
                                        uint32_t nand_page)
{
    aml_raw_nand_t *raw_nand = (aml_raw_nand_t*)ctx;
    uint32_t cmd;
    uint64_t daddr = raw_nand->data_buf_paddr;
    uint64_t iaddr = raw_nand->info_buf_paddr;
    zx_status_t status;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);
    uint32_t ecc_pagesize = 0; /* initialize to silence compiler */
    uint32_t ecc_pages;
    bool page0 = is_page0_nand_page(nand_page);

    if (!page0) {
        ecc_pagesize = aml_get_ecc_pagesize(raw_nand,
                                            raw_nand->controller_params.bch_mode);
        ecc_pages = raw_nand->writesize / ecc_pagesize;
        if (is_page0_nand_page(nand_page))
            return ZX_ERR_IO;
    } else
        ecc_pages = 1;
    if (data != NULL) {
        memcpy(raw_nand->data_buf, data, raw_nand->writesize);
        io_buffer_cache_flush(&raw_nand->data_buffer, 0,
                              raw_nand->writesize);
    }
    if (oob != NULL) {
        aml_set_oob_byte(raw_nand, oob, ecc_pages);
        io_buffer_cache_flush_invalidate(&raw_nand->info_buffer, 0,
                                         ecc_pages * sizeof(struct aml_info_format));
    }

    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_SEQIN, 0x00, nand_page,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    cmd = GENCMDDADDRL(AML_CMD_ADL, daddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDDADDRH(AML_CMD_ADH, daddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDIADDRL(AML_CMD_AIL, iaddr);
    writel(cmd, reg + P_NAND_CMD);
    cmd = GENCMDIADDRH(AML_CMD_AIH, iaddr);
    writel(cmd, reg + P_NAND_CMD);
    /* page0 needs randomization. so force it for page0 */
    if (page0 || raw_nand->controller_params.rand_mode)
        /*
         * Only need to set the seed if randomizing
         * is enabled.
         */
        aml_cmd_seed(raw_nand, nand_page);
    if (!page0)
        aml_cmd_m2n(raw_nand, ecc_pages, ecc_pagesize);
    else
        aml_cmd_m2n_page0(raw_nand);
    status = aml_wait_dma_finish(raw_nand);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: error from wait_dma_finish\n",
               __func__);
        return status;
    }
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_PAGEPROG, -1, -1,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    status = onfi_wait(&raw_nand->raw_nand_proto, AML_WRITE_PAGE_TIMEOUT);

    return status;
}

/*
 * Erase entry point into the Amlogic driver.
 * nandblock : NAND erase block address.
 */
static zx_status_t aml_erase_block(void* ctx, uint32_t nand_page) {
    aml_raw_nand_t* raw_nand = (aml_raw_nand_t*)ctx;
    zx_status_t status;

    /* nandblock has to be erasesize aligned */
    if (nand_page % raw_nand->erasesize_pages) {
        zxlogf(ERROR, "%s: NAND block %u must be a erasesize_pages (%u) multiple\n",
               __func__, nand_page, raw_nand->erasesize_pages);
        return ZX_ERR_INVALID_ARGS;
    }
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_ERASE1, -1, nand_page,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_ERASE2, -1, -1,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    status = onfi_wait(&raw_nand->raw_nand_proto, AML_ERASE_BLOCK_TIMEOUT);
    return status;
}

static zx_status_t aml_get_flash_type(aml_raw_nand_t* raw_nand) {
    uint8_t nand_maf_id, nand_dev_id;
    uint8_t id_data[8];
    struct nand_chip_table* nand_chip;

    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_RESET, -1, -1,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_READID, 0x00, -1,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    /* Read manufacturer and device IDs */
    nand_maf_id = aml_read_byte(&raw_nand->raw_nand_proto);
    nand_dev_id = aml_read_byte(&raw_nand->raw_nand_proto);
    /* Read again */
    onfi_command(&raw_nand->raw_nand_proto, NAND_CMD_READID, 0x00, -1,
                 raw_nand->chipsize, raw_nand->chip_delay,
                 (raw_nand->controller_params.options & NAND_BUSWIDTH_16));
    /* Read entire ID string */
    for (uint32_t i = 0; i < sizeof(id_data); i++)
        id_data[i] = aml_read_byte(&raw_nand->raw_nand_proto);
    if (id_data[0] != nand_maf_id || id_data[1] != nand_dev_id) {
        zxlogf(ERROR, "second ID read did not match %02x,%02x against %02x,%02x\n",
               nand_maf_id, nand_dev_id, id_data[0], id_data[1]);
    }

    zxlogf(INFO, "%s: manufacturer_id = %x, device_ide = %x\n",
           __func__, nand_maf_id, nand_dev_id);

    nand_chip = find_nand_chip_table(nand_maf_id, nand_dev_id);
    if (nand_chip == NULL) {
        zxlogf(ERROR, "%s: Cound not find matching NAND chip. NAND chip unsupported."
                      " This is FATAL\n",
               __func__);
        return ZX_ERR_UNAVAILABLE;
    }
    if (nand_chip->extended_id_nand) {
        /*
	 * Initialize pagesize, eraseblk size, oobsize and
	 * buswidth from extended parameters queried just now.
	 */
        uint8_t extid = id_data[3];

        raw_nand->writesize = 1024 << (extid & 0x03);
        extid >>= 2;
        /* Calc oobsize */
        raw_nand->oobsize = (8 << (extid & 0x01)) *
                            (raw_nand->writesize >> 9);
        extid >>= 2;
        /* Calc blocksize. Blocksize is multiples of 64KiB */
        raw_nand->erasesize = (64 * 1024) << (extid & 0x03);
        extid >>= 2;
        /* Get buswidth information */
        raw_nand->bus_width = (extid & 0x01) ? NAND_BUSWIDTH_16 : 0;
    } else {
        /*
	 * Initialize pagesize, eraseblk size, oobsize and
	 * buswidth from values in table.
	 */
        raw_nand->writesize = nand_chip->page_size;
        raw_nand->oobsize = nand_chip->oobsize;
        raw_nand->erasesize = nand_chip->erase_block_size;
        raw_nand->bus_width = nand_chip->bus_width;
    }
    raw_nand->erasesize_pages =
        raw_nand->erasesize / raw_nand->writesize;
    raw_nand->chipsize = nand_chip->chipsize;
    raw_nand->page_shift = ffs(raw_nand->writesize) - 1;

    /*
     * We found a matching device in our database, use it to
     * initialize. Adjust timings and set various parameters.
     */
    aml_adjust_timings(raw_nand,
                       nand_chip->timings.tRC_min,
                       nand_chip->timings.tREA_max,
                       nand_chip->timings.RHOH_min);
    /*
     * chip_delay is used onfi_command(), after sending down some commands
     * to the NAND chip.
     */
    raw_nand->chip_delay = nand_chip->chip_delay_us;
    zxlogf(INFO, "NAND %s %s: chip size = %lu(GB), page size = %u, oob size = %u\n"
           "eraseblock size = %u, chip delay (us) = %u\n",
           nand_chip->manufacturer_name, nand_chip->device_name,
           raw_nand->chipsize, raw_nand->writesize, raw_nand->oobsize, raw_nand->erasesize,
           raw_nand->chip_delay);
    return ZX_OK;
}

static int aml_raw_nand_irq_thread(void* arg) {
    zxlogf(INFO, "aml_raw_nand_irq_thread start\n");

    aml_raw_nand_t* raw_nand = arg;

    while (1) {
        zx_time_t slots;

        zx_status_t result = zx_interrupt_wait(raw_nand->irq_handle, &slots);
        if (result != ZX_OK) {
            zxlogf(ERROR,
                   "aml_raw_nand_irq_thread: zx_interrupt_wait got %d\n",
                   result);
            break;
        }
        /*
         * Wakeup blocked requester on
         * completion_wait(&raw_nand->req_completion, ZX_TIME_INFINITE);
         */
        completion_signal(&raw_nand->req_completion);
    }

    return 0;
}

static zx_status_t aml_get_nand_info(void* ctx, struct nand_info* nand_info) {
    aml_raw_nand_t* raw_nand = (aml_raw_nand_t*)ctx;
    uint64_t capacity;
    zx_status_t status = ZX_OK;

    nand_info->page_size = raw_nand->writesize;
    nand_info->pages_per_block = raw_nand->erasesize_pages;
    capacity = raw_nand->chipsize * (1024 * 1024);
    capacity /= raw_nand->erasesize;
    nand_info->num_blocks = (uint32_t)capacity;
    nand_info->ecc_bits = raw_nand->controller_params.ecc_strength;

    nand_info->nand_class = NAND_CLASS_PARTMAP;
    memset(&nand_info->partition_guid, 0, sizeof(nand_info->partition_guid));

    if (raw_nand->controller_params.user_mode == 2)
        nand_info->oob_size =
            (raw_nand->writesize /
             aml_get_ecc_pagesize(raw_nand, raw_nand->controller_params.bch_mode)) *
            2;
    else
        status = ZX_ERR_NOT_SUPPORTED;
    return status;
}

static raw_nand_protocol_ops_t aml_raw_nand_ops = {
    .read_page_hwecc = aml_read_page_hwecc,
    .write_page_hwecc = aml_write_page_hwecc,
    .erase_block = aml_erase_block,
    .get_nand_info = aml_get_nand_info,
    .cmd_ctrl = aml_cmd_ctrl,
    .read_byte = aml_read_byte,
};

static void aml_raw_nand_release(void* ctx) {
    aml_raw_nand_t* raw_nand = ctx;

    for (raw_nand_addr_window_t wnd = 0;
         wnd < ADDR_WINDOW_COUNT;
         wnd++)
        io_buffer_release(&raw_nand->mmio[wnd]);
    io_buffer_release(&raw_nand->data_buffer);
    io_buffer_release(&raw_nand->info_buffer);
    zx_handle_close(raw_nand->bti_handle);
    free(raw_nand);
}

static void aml_set_encryption(aml_raw_nand_t* raw_nand) {
    uint32_t cfg;
    volatile uint8_t* reg = (volatile uint8_t*)
        io_buffer_virt(&raw_nand->mmio[NANDREG_WINDOW]);

    cfg = readl(reg + P_NAND_CFG);
    cfg |= (1 << 17);
    writel(cfg, reg + P_NAND_CFG);
}

static zx_status_t aml_read_page0(aml_raw_nand_t* raw_nand,
                                  void* data,
                                  void* oob,
                                  uint32_t nand_page,
                                  int* ecc_correct,
                                  int retries) {
    zx_status_t status;

    retries++;
    do {
        status = aml_read_page_hwecc(raw_nand, data, oob,
                                     nand_page, ecc_correct);
    } while (status != ZX_OK && --retries > 0);
    if (status != ZX_OK)
        zxlogf(ERROR, "%s: Read error\n", __func__);
    return status;
}

/*
 * Read one of the page0 pages, and use the result to init
 * ECC algorithm and rand-mode.
 */
static zx_status_t aml_nand_init_from_page0(aml_raw_nand_t* raw_nand) {
    zx_status_t status;
    char* data;
    nand_page0_t* page0;
    int ecc_correct;

    data = malloc(raw_nand->writesize);
    if (data == NULL) {
        zxlogf(ERROR, "%s: Cannot allocate memory to read in Page0\n", __func__);
        return ZX_ERR_NO_MEMORY;
    }
    /*
     * There are 8 copies of page0 spaced apart by 128 pages
     * starting at Page 0. Read the first we can.
     */
    for (uint32_t i = 0; i < 7; i++) {
        status = aml_read_page0(raw_nand, data, NULL, i * 128,
                                &ecc_correct, 3);
        if (status == ZX_OK)
            break;
    }
    if (status != ZX_OK) {
        /*
         * Could not read any of the page0 copies. This is a fatal
         * error.
         */
        free(data);
        zxlogf(ERROR, "%s: Page0 Read (all copies) failed\n", __func__);
        return status;
    }

    page0 = (nand_page0_t*)data;
    raw_nand->controller_params.rand_mode =
        (page0->nand_setup.cfg.d32 >> 19) & 0x1;
    raw_nand->controller_params.bch_mode =
        (page0->nand_setup.cfg.d32 >> 14) & 0x7;
    zxlogf(INFO, "%s: NAND BCH Mode is %s\n", __func__,
           aml_ecc_string(raw_nand->controller_params.bch_mode));
    free(data);
    return ZX_OK;
}

static zx_status_t aml_raw_nand_allocbufs(aml_raw_nand_t* raw_nand) {
    zx_status_t status;

    status = pdev_get_bti(&raw_nand->pdev, 0, &raw_nand->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "raw_nand_test_allocbufs: pdev_get_bti failed (%d)\n",
               status);
        return status;
    }
    status = io_buffer_init(&raw_nand->data_buffer,
                            raw_nand->bti_handle,
                            raw_nand->writesize,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR,
               "raw_nand_test_allocbufs: io_buffer_init(data_buffer) failed\n");
        zx_handle_close(raw_nand->bti_handle);
        return status;
    }
    ZX_DEBUG_ASSERT(raw_nand->writesize > 0);
    status = io_buffer_init(&raw_nand->info_buffer,
                            raw_nand->bti_handle,
                            raw_nand->writesize,
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR,
               "raw_nand_test_allocbufs: io_buffer_init(info_buffer) failed\n");
        io_buffer_release(&raw_nand->data_buffer);
        zx_handle_close(raw_nand->bti_handle);
        return status;
    }
    raw_nand->data_buf = io_buffer_virt(&raw_nand->data_buffer);
    raw_nand->info_buf = io_buffer_virt(&raw_nand->info_buffer);
    raw_nand->data_buf_paddr = io_buffer_phys(&raw_nand->data_buffer);
    raw_nand->info_buf_paddr = io_buffer_phys(&raw_nand->info_buffer);
    return ZX_OK;
}

static zx_status_t aml_nand_init(aml_raw_nand_t* raw_nand) {
    zx_status_t status;

    /*
     * Do nand scan to get manufacturer and other info
     */
    status = aml_get_flash_type(raw_nand);
    if (status != ZX_OK)
        return status;
    raw_nand->controller_params.ecc_strength = aml_params.ecc_strength;
    raw_nand->controller_params.user_mode = aml_params.user_mode;
    raw_nand->controller_params.rand_mode = aml_params.rand_mode;
    raw_nand->controller_params.options = NAND_USE_BOUNCE_BUFFER;
    raw_nand->controller_params.bch_mode = aml_params.bch_mode;

    /*
     * Note on OOB byte settings.
     * The default config for OOB is 2 bytes per OOB page. This is the
     * settings we use. So nothing to be done for OOB. If we ever need
     * to switch to 16 bytes of OOB per NAND page, we need to set the
     * right bits in the CFG register/
     */

    status = aml_raw_nand_allocbufs(raw_nand);
    if (status != ZX_OK)
        return status;

    /*
     * Read one of the copies of page0, and use that to initialize
     * ECC algorithm and rand-mode.
     */
    status = aml_nand_init_from_page0(raw_nand);

    /* Force chip_select to 0 */
    raw_nand->chip_select = chipsel[0];

    return status;
}

static void aml_raw_nand_unbind(void* ctx) {
    aml_raw_nand_t* raw_nand = ctx;

    zx_interrupt_destroy(raw_nand->irq_handle);
    thrd_join(raw_nand->irq_thread, NULL);
    zx_handle_close(raw_nand->irq_handle);
    device_remove(raw_nand->zxdev);
}

static zx_protocol_device_t raw_nand_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = aml_raw_nand_unbind,
    .release = aml_raw_nand_release,
};

static zx_status_t aml_raw_nand_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    aml_raw_nand_t* raw_nand = calloc(1, sizeof(aml_raw_nand_t));

    if (!raw_nand) {
        return ZX_ERR_NO_MEMORY;
    }

    raw_nand->req_completion = COMPLETION_INIT;

    if ((status = device_get_protocol(parent,
                                      ZX_PROTOCOL_PLATFORM_DEV,
                                      &raw_nand->pdev)) != ZX_OK) {
        zxlogf(ERROR,
               "aml_raw_nand_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        free(raw_nand);
        return status;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&raw_nand->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_raw_nand_bind: pdev_get_device_info failed\n");
        free(raw_nand);
        return status;
    }

    /* Map all of the mmio windows that we need */
    for (raw_nand_addr_window_t wnd = 0;
         wnd < ADDR_WINDOW_COUNT;
         wnd++) {
        status = pdev_map_mmio_buffer(&raw_nand->pdev,
                                      wnd,
                                      ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                      &raw_nand->mmio[wnd]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_raw_nand_bind: pdev_map_mmio_buffer failed %d\n",
                   status);
            for (raw_nand_addr_window_t j = 0; j < wnd; j++)
                io_buffer_release(&raw_nand->mmio[j]);
            free(raw_nand);
            return status;
        }
    }

    status = pdev_map_interrupt(&raw_nand->pdev, 0, &raw_nand->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_raw_nand_bind: pdev_map_interrupt failed %d\n",
               status);
        goto fail;
    }

    raw_nand->raw_nand_proto.ops = &aml_raw_nand_ops;
    raw_nand->raw_nand_proto.ctx = raw_nand;
    /*
     * This creates a device that a top level (controller independent)
     * raw_nand driver can bind to.
     */
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-raw_nand",
        .ctx = raw_nand,
        .ops = &raw_nand_device_proto,
        .proto_id = ZX_PROTOCOL_RAW_NAND,
        .proto_ops = &aml_raw_nand_ops,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    status = device_add(parent, &args, &raw_nand->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_raw_nand_bind: device_add failed\n");
        zx_handle_close(raw_nand->irq_handle);
        goto fail;
    }

    int rc = thrd_create_with_name(&raw_nand->irq_thread,
                                   aml_raw_nand_irq_thread,
                                   raw_nand, "aml_raw_nand_irq_thread");
    if (rc != thrd_success) {
        zx_handle_close(raw_nand->irq_handle);
        status = thrd_status_to_zx_status(rc);
        goto fail;
    }

    /*
     * Do the rest of the init here, instead of up top in the irq
     * thread, because the init needs for irq's to work.
     */
    aml_clock_init(raw_nand);
    status = aml_nand_init(raw_nand);
    if (status != ZX_OK) {
        zxlogf(ERROR,
               "aml_raw_nand_bind: aml_nand_init() failed - This is FATAL\n");
        zx_interrupt_destroy(raw_nand->irq_handle);
        thrd_join(raw_nand->irq_thread, NULL);
        device_remove(raw_nand->zxdev);
        goto fail;
    }

    zxlogf(ERROR, "aml_raw_nand_bind: Making device visible\n");

    /*
     * device was added invisible, now that init has completed,
     * flip the switch, allowing the upper layer nand driver to
     * bind to us.
     */
    device_make_visible(raw_nand->zxdev);

    return status;

fail:
    for (raw_nand_addr_window_t wnd = 0;
         wnd < ADDR_WINDOW_COUNT;
         wnd++)
        io_buffer_release(&raw_nand->mmio[wnd]);
    free(raw_nand);
    return status;
}

static zx_driver_ops_t aml_raw_nand_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_raw_nand_bind,
};

ZIRCON_DRIVER_BEGIN(aml_raw_nand, aml_raw_nand_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_RAW_NAND),
    ZIRCON_DRIVER_END(aml_raw_nand)
