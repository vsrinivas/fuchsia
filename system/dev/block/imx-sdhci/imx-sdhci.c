// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standard Includes
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdhci.h>
#include <hw/sdmmc.h>
#include <zircon/types.h>


// Zircon Includes
#include <zircon/threads.h>
#include <zircon/assert.h>
#include <sync/completion.h>
#include <pretty/hexdump.h>

#include "imx-sdhci.h"

// Uncomment to disable interrupts
// #define ENABLE_POLLING

// Uncomment to disable DMA Mode
// #define DISABLE_DMA

// Uncomment to print logs at all levels
// #define SDHCI_LOG_ALL 1

#ifdef SDHCI_LOG_ALL
#define SDHCI_ERROR(fmt, ...)       zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_INFO(fmt, ...)        zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_TRACE(fmt, ...)       zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_FUNC_ENTRY_LOG        zxlogf(ERROR, "[%s %d]\n", __func__, __LINE__)
#else
#define SDHCI_ERROR(fmt, ...)       zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_INFO(fmt, ...)        zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_TRACE(fmt, ...)       zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SDHCI_FUNC_ENTRY_LOG        zxlogf(TRACE, "[%s %d]\n", __func__, __LINE__)
#endif

#define PAGE_MASK   (PAGE_SIZE - 1ull)
#define SD_FREQ_SETUP_HZ  400000
#define MAX_TUNING_COUNT 40

typedef struct sdhci_adma64_desc {
    union {
        struct {
            uint8_t valid : 1;
            uint8_t end   : 1;
            uint8_t intr  : 1;
            uint8_t rsvd0 : 1;
            uint8_t act1  : 1;
            uint8_t act2  : 1;
            uint8_t rsvd1 : 2;
            uint8_t rsvd2;
        } __PACKED;
        uint16_t attr;
    } __PACKED;
    uint16_t length;
    uint32_t address;
} __PACKED sdhci_adma64_desc_t;

static_assert(sizeof(sdhci_adma64_desc_t) == 8, "unexpected ADMA2 descriptor size");

// 64k max per descriptor
#define ADMA2_DESC_MAX_LENGTH   0x10000 // 64k
// for 2M max transfer size for fully discontiguous
// also see SDMMC_PAGES_COUNT in ddk/protocol/sdmmc.h
#define DMA_DESC_COUNT          512



// TODO: Get base block from hardware registers
#define IMX8M_SDHCI_BASE_CLOCK  200000000

typedef struct imx_sdhci_device {
    platform_device_protocol_t  pdev;
    platform_bus_protocol_t     pbus;
    zx_device_t*                zxdev;
    io_buffer_t                 mmios;
    zx_handle_t                 irq_handle;
    gpio_protocol_t             gpio;
    volatile imx_sdhci_regs_t*  regs;
    uint64_t                    regs_size;
    zx_handle_t                 regs_handle;
    zx_handle_t                 bti_handle;

    // DMA descriptors
    io_buffer_t                 iobuf;
    sdhci_adma64_desc_t*        descs;

    mtx_t                       mtx;                // Held when a command or action is in progress.
    sdmmc_req_t*                cmd_req;            // Current command request
    sdmmc_req_t*                data_req;           // Current data line request
    uint16_t                    data_blockid;       // Current block id to transfer (PIO)
    bool                        data_done;          // Set to true if the data stage completed
                                                    // before the cmd stage
    completion_t                req_completion;     // used to signal request complete
    sdmmc_host_info_t           info;               // Controller info
    uint32_t                    base_clock;         // Base clock rate
    bool                        ddr_mode;           // DDR Mode enable flag
    bool                        dma_mode;           // Flag used to switch between dma and pio mode
} imx_sdhci_device_t;

static const uint32_t error_interrupts = (
    IMX_SDHC_INT_STAT_DMAE  |
    IMX_SDHC_INT_STAT_TNE   |
    IMX_SDHC_INT_STAT_AC12E |
    IMX_SDHC_INT_STAT_DEBE  |
    IMX_SDHC_INT_STAT_DCE   |
    IMX_SDHC_INT_STAT_DTOE  |
    IMX_SDHC_INT_STAT_CIE   |
    IMX_SDHC_INT_STAT_CEBE  |
    IMX_SDHC_INT_STAT_CCE   |
    IMX_SDHC_INT_STAT_CTOE
);

static const uint32_t normal_interrupts = (
    IMX_SDHC_INT_STAT_BRR   |
    IMX_SDHC_INT_STAT_BWR   |
    IMX_SDHC_INT_STAT_TC    |
    IMX_SDHC_INT_STAT_CC
);

static const uint32_t dma_normal_interrupts = (
    IMX_SDHC_INT_STAT_TC    |
    IMX_SDHC_INT_STAT_CC
);

static void esdhc_dump(imx_sdhci_device_t* dev)
{
    SDHCI_ERROR("#######################\n");
    SDHCI_ERROR("Dumping Registers\n\n");
    SDHCI_ERROR("    ds_addr = 0x%x\n", dev->regs->ds_addr);
    SDHCI_ERROR("    blk_att = 0x%x\n", dev->regs->blk_att);
    SDHCI_ERROR("    cmd_arg = 0x%x\n", dev->regs->cmd_arg);
    SDHCI_ERROR("    cmd_xfr_typ = 0x%x\n", dev->regs->cmd_xfr_typ);
    SDHCI_ERROR("    cmd_rsp0 = 0x%x\n", dev->regs->cmd_rsp0);
    SDHCI_ERROR("    cmd_rsp1 = 0x%x\n", dev->regs->cmd_rsp1);
    SDHCI_ERROR("    cmd_rsp2 = 0x%x\n", dev->regs->cmd_rsp2);
    SDHCI_ERROR("    cmd_rsp3 = 0x%x\n", dev->regs->cmd_rsp3);
    SDHCI_ERROR("    data_buff_acc_port = 0x%x\n", dev->regs->data_buff_acc_port);
    SDHCI_ERROR("    pres_state = 0x%x\n", dev->regs->pres_state);
    SDHCI_ERROR("    prot_ctrl = 0x%x\n", dev->regs->prot_ctrl);
    SDHCI_ERROR("    sys_ctrl = 0x%x\n", dev->regs->sys_ctrl);
    SDHCI_ERROR("    int_status = 0x%x\n", dev->regs->int_status);
    SDHCI_ERROR("    int_status_en = 0x%x\n", dev->regs->int_status_en);
    SDHCI_ERROR("    int_signal_en = 0x%x\n", dev->regs->int_signal_en);
    SDHCI_ERROR("    autocmd12_err_status = 0x%x\n", dev->regs->autocmd12_err_status);
    SDHCI_ERROR("    host_ctrl_cap = 0x%x\n", dev->regs->host_ctrl_cap);
    SDHCI_ERROR("    wtmk_lvl = 0x%x\n", dev->regs->wtmk_lvl);
    SDHCI_ERROR("    mix_ctrl = 0x%x\n", dev->regs->mix_ctrl);
    SDHCI_ERROR("    force_event = 0x%x\n", dev->regs->force_event);
    SDHCI_ERROR("    adma_err_status = 0x%x\n", dev->regs->adma_err_status);
    SDHCI_ERROR("    adma_sys_addr = 0x%x\n", dev->regs->adma_sys_addr);
    SDHCI_ERROR("    dll_ctrl = 0x%x\n", dev->regs->dll_ctrl);
    SDHCI_ERROR("    dll_status = 0x%x\n", dev->regs->dll_status);
    SDHCI_ERROR("    clk_tune_ctrl_status = 0x%x\n", dev->regs->clk_tune_ctrl_status);
    SDHCI_ERROR("    strobe_dll_ctrl = 0x%x\n", dev->regs->strobe_dll_ctrl);
    SDHCI_ERROR("    strobe_dll_status = 0x%x\n", dev->regs->strobe_dll_status);
    SDHCI_ERROR("    vend_spec = 0x%x\n", dev->regs->vend_spec);
    SDHCI_ERROR("    mmc_boot = 0x%x\n", dev->regs->mmc_boot);
    SDHCI_ERROR("    vend_spec2 = 0x%x\n", dev->regs->vend_spec2);
    SDHCI_ERROR("    tuning_ctrl = 0x%x\n", dev->regs->tuning_ctrl);
    SDHCI_ERROR("\n\n");
}

static void imx_decode_irq_error(uint32_t err) {

    if(err & IMX_SDHC_INT_EN_DMAEN) {
        SDHCI_ERROR("    Error:DMAEN...\n");
    }

    if(err & IMX_SDHC_INT_EN_TNE) {
        SDHCI_ERROR("    Error:TNE...\n");
    }

    if(err & IMX_SDHC_INT_EN_AC12E) {
        SDHCI_ERROR("    Error:AC12E...\n");
    }

    if(err & IMX_SDHC_INT_EN_DEBE) {
        SDHCI_ERROR("    Error:DEBE...\n");
    }

    if(err & IMX_SDHC_INT_EN_DCE) {
        SDHCI_ERROR("    Error:DCE...\n");
    }

    if(err & IMX_SDHC_INT_EN_DTOE) {
        SDHCI_ERROR("    Error:DTOE...\n");
    }

    if(err & IMX_SDHC_INT_EN_CIE) {
        SDHCI_ERROR("    Error:CIE...\n");
    }

    if(err & IMX_SDHC_INT_EN_CEBE) {
        SDHCI_ERROR("    Error:CEBE...\n");
    }

    if(err & IMX_SDHC_INT_EN_CCE) {
        SDHCI_ERROR("    Error:CCE...\n");
    }

    if(err & IMX_SDHC_INT_EN_CTOE) {
        SDHCI_ERROR("    Error:CTOE...\n");
    }

}

static bool imx_sdmmc_cmd_rsp_busy(uint32_t cmd_flags) {
    return cmd_flags & SDMMC_RESP_LEN_48B;
}

static bool imx_sdmmc_has_data(uint32_t cmd_flags) {
    return cmd_flags & SDMMC_RESP_DATA_PRESENT;
}

static uint32_t imx_sdhci_prepare_cmd(sdmmc_req_t* req) {
    uint32_t cmd = SDHCI_CMD_IDX(req->cmd_idx);
    uint32_t cmd_flags = req->cmd_flags;
    uint32_t sdmmc_sdhci_map[][2] = { {SDMMC_RESP_CRC_CHECK, SDHCI_CMD_RESP_CRC_CHECK},
                                      {SDMMC_RESP_CMD_IDX_CHECK, SDHCI_CMD_RESP_CMD_IDX_CHECK},
                                      {SDMMC_RESP_DATA_PRESENT, SDHCI_CMD_RESP_DATA_PRESENT},
                                      {SDMMC_CMD_DMA_EN, SDHCI_CMD_DMA_EN},
                                      {SDMMC_CMD_BLKCNT_EN, SDHCI_CMD_BLKCNT_EN},
                                      {SDMMC_CMD_AUTO12, SDHCI_CMD_AUTO12},
                                      {SDMMC_CMD_AUTO23, SDHCI_CMD_AUTO23},
                                      {SDMMC_CMD_READ, SDHCI_CMD_READ},
                                      {SDMMC_CMD_MULTI_BLK, SDHCI_CMD_MULTI_BLK}
                                    };
    if (cmd_flags & SDMMC_RESP_LEN_EMPTY) {
        cmd |= SDHCI_CMD_RESP_LEN_EMPTY;
    } else if (cmd_flags & SDMMC_RESP_LEN_136) {
        cmd |= SDHCI_CMD_RESP_LEN_136;
    } else if (cmd_flags & SDMMC_RESP_LEN_48) {
        cmd |= SDHCI_CMD_RESP_LEN_48;
    } else if (cmd_flags & SDMMC_RESP_LEN_48B) {
        cmd |= SDHCI_CMD_RESP_LEN_48B;
    }
    if (cmd_flags & SDMMC_CMD_TYPE_NORMAL) {
        cmd |= SDHCI_CMD_TYPE_NORMAL;
    } else if (cmd_flags & SDMMC_CMD_TYPE_SUSPEND) {
        cmd |= SDHCI_CMD_TYPE_SUSPEND;
    } else if (cmd_flags & SDMMC_CMD_TYPE_RESUME) {
        cmd |= SDHCI_CMD_TYPE_RESUME;
    } else if (cmd_flags & SDMMC_CMD_TYPE_ABORT) {
        cmd |= SDHCI_CMD_TYPE_ABORT;
    }
    for (unsigned i = 0; i < sizeof(sdmmc_sdhci_map)/sizeof(*sdmmc_sdhci_map); i++) {
        if (cmd_flags & sdmmc_sdhci_map[i][0]) {
            cmd |= sdmmc_sdhci_map[i][1];
        }
    }
    return cmd;
}

static zx_status_t imx_sdhci_wait_for_reset(imx_sdhci_device_t* dev,
                                            const uint32_t mask, zx_time_t timeout) {
    zx_time_t deadline = zx_clock_get_monotonic() + timeout;
    while (true) {
        if (!(dev->regs->sys_ctrl & mask)) {
            break;
        }
        if (zx_clock_get_monotonic() > deadline) {
            SDHCI_ERROR("time out while waiting for reset\n");
            return ZX_ERR_TIMED_OUT;
        }
    }
    return ZX_OK;
}

static void imx_sdhci_complete_request_locked(imx_sdhci_device_t* dev, sdmmc_req_t* req,
                                                zx_status_t status) {
    SDHCI_TRACE("complete cmd 0x%08x status %d\n", req->cmd_idx, status);

    // Disable interrupts when no pending transfer
    dev->regs->int_signal_en = 0;

    dev->cmd_req = NULL;
    dev->data_req = NULL;
    dev->data_blockid = 0;
    dev->data_done = false;

    req->status = status;
    completion_signal(&dev->req_completion);
}

static void imx_sdhci_cmd_stage_complete_locked(imx_sdhci_device_t* dev) {
    SDHCI_TRACE("Got CC interrupt\n");

    if (!dev->cmd_req) {
        SDHCI_TRACE("Spurious CC interupt\n");
        return;
    }

    sdmmc_req_t* req = dev->cmd_req;
    volatile struct imx_sdhci_regs* regs = dev->regs;
    uint32_t cmd = imx_sdhci_prepare_cmd(req);

    // Read the response data
    if (cmd & SDHCI_CMD_RESP_LEN_136) {
        req->response[0] = (regs->cmd_rsp0 << 8);
        req->response[1] = (regs->cmd_rsp1 << 8) | ((regs->cmd_rsp0 >> 24) & 0xFF);
        req->response[2] = (regs->cmd_rsp2 << 8) | ((regs->cmd_rsp1 >> 24) & 0xFF);
        req->response[3] = (regs->cmd_rsp3 << 8) | ((regs->cmd_rsp2 >> 24) & 0xFF);
    } else if (cmd & (SDHCI_CMD_RESP_LEN_48 | SDHCI_CMD_RESP_LEN_48B)) {
        req->response[0] = regs->cmd_rsp0;
        req->response[1] = regs->cmd_rsp1;
    }

    // We're done if the command has no data stage or if the data stage completed early
    if (!dev->data_req || dev->data_done) {
        imx_sdhci_complete_request_locked(dev, dev->cmd_req, ZX_OK);
    } else {
        dev->cmd_req = NULL;
    }
}

static void imx_sdhci_data_stage_read_ready_locked(imx_sdhci_device_t* dev) {
    SDHCI_TRACE("Got BRR Interrupts\n");

    if (!dev->data_req || !imx_sdmmc_has_data(dev->data_req->cmd_flags)) {
        SDHCI_ERROR("Spurious BRR Interrupt. %p\n", dev->data_req);
        return;
    }

    if (dev->data_req->cmd_idx == MMC_SEND_TUNING_BLOCK) {
        // tuning commnad is done here
        imx_sdhci_complete_request_locked(dev, dev->data_req, ZX_OK);
        return;
    }

    sdmmc_req_t* req = dev->data_req;

    // Sequentially read each block
    for (size_t byteid = 0; byteid < req->blocksize; byteid += 4) {
        const size_t offset = dev->data_blockid * req->blocksize + byteid;
        uint32_t* wrd = req->virt + offset;
        *wrd = dev->regs->data_buff_acc_port; //TODO: Can't read this if DMA is enabled!
    }
    dev->data_blockid += 1;
}

static void imx_sdhci_data_stage_write_ready_locked(imx_sdhci_device_t* dev) {
    SDHCI_TRACE("Got BWR Interrupt\n");

    if (!dev->data_req || !imx_sdmmc_has_data(dev->data_req->cmd_flags)) {
        SDHCI_TRACE("Spurious BWR Interrupt\n");
        return;
    }

    sdmmc_req_t* req = dev->data_req;

    // Sequentially write each block
    for (size_t byteid = 0; byteid < req->blocksize; byteid += 4) {
        const size_t offset = dev->data_blockid * req->blocksize + byteid;
        uint32_t* wrd = req->virt + offset;
        dev->regs->data_buff_acc_port = *wrd; //TODO: Can't write if DMA is enabled
    }
    dev->data_blockid += 1;
}

static void imx_sdhci_transfer_complete_locked(imx_sdhci_device_t* dev) {
    SDHCI_TRACE("Got TC Interrupt\n");
    if (!dev->data_req) {
        SDHCI_TRACE("Spurious TC Interrupt\n");
        return;
    }

    if (dev->cmd_req) {
        dev->data_done = true;
    } else {
        imx_sdhci_complete_request_locked(dev, dev->data_req, ZX_OK);
    }
}

static void imx_sdhci_error_recovery_locked(imx_sdhci_device_t* dev) {
    // Reset internal state machines
    dev->regs->sys_ctrl |= IMX_SDHC_SYS_CTRL_RSTC;
    imx_sdhci_wait_for_reset(dev, IMX_SDHC_SYS_CTRL_RSTC, ZX_SEC(1));
    dev->regs->sys_ctrl |= IMX_SDHC_SYS_CTRL_RSTD;
    imx_sdhci_wait_for_reset(dev, IMX_SDHC_SYS_CTRL_RSTD, ZX_SEC(1));

    // Complete any pending txn with error status
    if (dev->cmd_req != NULL) {
        imx_sdhci_complete_request_locked(dev, dev->cmd_req, ZX_ERR_IO);
    } else if (dev->data_req != NULL) {
        imx_sdhci_complete_request_locked(dev, dev->data_req, ZX_ERR_IO);
    }
}

static uint32_t get_clock_divider(imx_sdhci_device_t* dev,
                                    const uint32_t base_clock, const uint32_t target_rate) {
    uint32_t pre_div = 1;
    uint32_t div = 1;

    if (target_rate >= base_clock) {
        // A clock divider of 0 means "don't divide the clock"
        // If the base clock is already slow enough to use as the SD clock then
        // we don't need to divide it any further.
        return 0;
    }

    if (dev->ddr_mode) {
        pre_div = 2;
    }

    SDHCI_TRACE("base %d, pre_div %d, div = %d, target_rate %d\n",
        base_clock, pre_div, div, target_rate);
    while (base_clock / pre_div / 16 > target_rate && pre_div < 256) {
        SDHCI_TRACE("base %d, pre_div %d, div = %d, target_rate %d\n",
            base_clock, pre_div, div, target_rate);
        pre_div *= 2;
    }

    while (base_clock / pre_div / div > target_rate && div < 16) {
        SDHCI_TRACE("base %d, pre_div %d, div = %d, target_rate %d\n",
            base_clock, pre_div, div, target_rate);
        div++;
    }

    SDHCI_TRACE("base %d, pre_div %d, div = %d, target_rate %d\n",
        base_clock, pre_div, div, target_rate);

    if(dev->ddr_mode) {
        pre_div >>= 2;
    } else {
        pre_div >>= 1;
    }
    div -= 1;

    return (((pre_div & 0xFF) << 16)| (div & 0xF));
}

#ifndef ENABLE_POLLING
static int imx_sdhci_irq_thread(void *args) {
    zx_status_t wait_res;
    imx_sdhci_device_t* dev = (imx_sdhci_device_t*)args;
    volatile struct imx_sdhci_regs* regs = dev->regs;
    zx_handle_t irq_handle = dev->irq_handle;
    while(true) {
        regs->int_signal_en = normal_interrupts | error_interrupts;
        wait_res = zx_interrupt_wait(irq_handle, NULL);
        if (wait_res != ZX_OK) {
            SDHCI_ERROR("sdhci: interrupt wait failed with retcode = %d\n", wait_res);
            break;
        }

        const uint32_t irq = regs->int_status;
        SDHCI_TRACE("got irq 0x%08x[stat 0x%08x en 0x%08x sig 0x%08x\n",irq, regs->int_status,
                                                    regs->int_status_en, regs->int_signal_en);


        // disable interrupts generation since we only process one at a time
        // int_status_en is still enabled, so we won't lose any interrupt info
        regs->int_signal_en = 0; // disable for now

        // Acknowledge the IRQs that we stashed.
        regs->int_status = irq;

        mtx_lock(&dev->mtx);
        if (irq & error_interrupts) {
            SDHCI_ERROR("IRQ ERROR: 0x%x\n", irq);
            imx_decode_irq_error(irq);
            esdhc_dump(dev);
            if (irq & IMX_SDHC_INT_STAT_DMAE) {
                SDHCI_TRACE("ADMA error 0x%x ADMAADDR0 0x%x\n",
                regs->adma_err_status, regs->adma_sys_addr);
            }
            imx_sdhci_error_recovery_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_CC) {
            imx_sdhci_cmd_stage_complete_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_BRR) {
            imx_sdhci_data_stage_read_ready_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_BWR) {
            imx_sdhci_data_stage_write_ready_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_TC) {
            imx_sdhci_transfer_complete_locked(dev);
        }
        mtx_unlock(&dev->mtx);
    }
    return ZX_OK;
}
#endif

static zx_status_t imx_sdhci_build_dma_desc(imx_sdhci_device_t* dev, sdmmc_req_t* req) {
    SDHCI_FUNC_ENTRY_LOG;
    uint64_t req_len = req->blockcount * req->blocksize;
    bool is_read = req->cmd_flags & SDMMC_CMD_READ;

    uint64_t pagecount = ((req->buf_offset & PAGE_MASK) + req_len + PAGE_MASK) /
                           PAGE_SIZE;
    if (pagecount > SDMMC_PAGES_COUNT) {
        SDHCI_ERROR("too many pages %lu vs %lu\n", pagecount, SDMMC_PAGES_COUNT);
        return ZX_ERR_INVALID_ARGS;
    }

    // pin the vmo
    zx_paddr_t phys[SDMMC_PAGES_COUNT];
    zx_handle_t pmt;
    // offset_vmo is converted to bytes by the sdmmc layer
    uint32_t options = is_read ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
    zx_status_t st = zx_bti_pin(dev->bti_handle, options, req->dma_vmo,
                                req->buf_offset & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, phys, pagecount, &pmt);
    if (st != ZX_OK) {
        SDHCI_ERROR("error %d bti_pin\n", st);
        return st;
    }
    // cache this for zx_pmt_unpin() later
    req->pmt = pmt;

    phys_iter_buffer_t buf = {
        .phys = phys,
        .phys_count = pagecount,
        .length = req_len,
        .vmo_offset = req->buf_offset,
    };
    phys_iter_t iter;
    phys_iter_init(&iter, &buf, ADMA2_DESC_MAX_LENGTH);

    int count = 0;
    size_t length;
    zx_paddr_t paddr;
    sdhci_adma64_desc_t* desc = dev->descs;
    for (;;) {
        length = phys_iter_next(&iter, &paddr);
        if (length == 0) {
            if (desc != dev->descs) {
                desc -= 1;
                desc->end = 1; // set end bit on the last descriptor
                break;
            } else {
                SDHCI_TRACE("empty descriptor list!\n");
                return ZX_ERR_NOT_SUPPORTED;
            }
        } else if (length > ADMA2_DESC_MAX_LENGTH) {
            SDHCI_TRACE("chunk size > %zu is unsupported\n", length);
            return ZX_ERR_NOT_SUPPORTED;
        } else if ((++count) > DMA_DESC_COUNT) {
            SDHCI_TRACE("request with more than %zd chunks is unsupported\n",
                    length);
            return ZX_ERR_NOT_SUPPORTED;
        }
        desc->length = length & 0xffff; // 0 = 0x10000 bytes
        desc->address = paddr;
        desc->attr = 0;
        desc->valid = 1;
        desc->act2 = 1; // transfer data
        desc += 1;
    }

    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        desc = dev->descs;
        do {
            SDHCI_TRACE("desc: addr=0x%" PRIx32 " length=0x%04x attr=0x%04x\n",
                         desc->address, desc->length, desc->attr);
        } while (!(desc++)->end);
    }
    return ZX_OK;
}

static zx_status_t imx_sdhci_start_req_locked(imx_sdhci_device_t* dev, sdmmc_req_t* req) {
    volatile struct imx_sdhci_regs* regs = dev->regs;
    const uint32_t arg = req->arg;
    const uint16_t blkcnt = req->blockcount;
    const uint16_t blksiz = req->blocksize;
    uint32_t cmd = imx_sdhci_prepare_cmd(req);
    bool has_data = imx_sdmmc_has_data(req->cmd_flags);

    if (req->use_dma && !dev->dma_mode) {
        SDHCI_INFO("we don't support dma yet\t");
        return ZX_ERR_NOT_SUPPORTED;
    }

    SDHCI_TRACE("start_req cmd=0x%08x (data %d dma %d bsy %d) blkcnt %u blksiz %u\n",
                  cmd, has_data, req->use_dma, imx_sdmmc_cmd_rsp_busy(cmd), blkcnt, blksiz);

    // Every command requires that the Commnad Inhibit bit is unset
    uint32_t inhibit_mask = IMX_SDHC_PRES_STATE_CIHB;

    // Busy type commands must also wait for the DATA Inhibit to be 0 unless it's an abort
    // command which can be issued with the data lines active
    if (((cmd & SDMMC_RESP_LEN_48B) == SDMMC_RESP_LEN_48B) &&
        ((cmd & SDMMC_CMD_TYPE_ABORT) == 0)) {
        inhibit_mask |= IMX_SDHC_PRES_STATE_CDIHB;
    }

    // Wait for the inhibit masks from above to become 0 before issuing the command
    while(regs->pres_state & inhibit_mask) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    zx_status_t st = ZX_OK;
    if (has_data) {
        if (req->use_dma) {
            st = imx_sdhci_build_dma_desc(dev, req);
            if (st != ZX_OK) {
                SDHCI_ERROR("Could not build DMA Descriptor\n");
                return st;
            }
            zx_paddr_t desc_phys = io_buffer_phys(&dev->iobuf);
            io_buffer_cache_flush(&dev->iobuf, 0,
                          DMA_DESC_COUNT * sizeof(sdhci_adma64_desc_t));
            regs->adma_sys_addr = (uint32_t) desc_phys;
            dev->regs->prot_ctrl &= ~(IMX_SDHC_PROT_CTRL_DMASEL_MASK);
            dev->regs->prot_ctrl |= IMX_SDHC_PROT_CTRL_DMASEL_ADMA2;
            regs->adma_err_status = 0;
            regs->mix_ctrl |= IMX_SDHC_MIX_CTRL_DMAEN;
        } else {
            dev->regs->prot_ctrl &= ~(IMX_SDHC_PROT_CTRL_DMASEL_MASK);
        }
        if (cmd & SDHCI_CMD_MULTI_BLK) {
            cmd |= SDHCI_CMD_AUTO12;
        }
    }

    regs->blk_att = (blksiz | (blkcnt << 16));
    dev->regs->wtmk_lvl = (blksiz/4) | (blksiz/4) << 16;

    regs->cmd_arg = arg;

    // Clear any pending interrupts before starting the transaction
    regs->int_status = 0xFFFFFFFF;

    if (req->use_dma) {
        // Unmask and enable interrupts
        regs->int_signal_en = error_interrupts | dma_normal_interrupts;
        regs->int_status_en = error_interrupts | dma_normal_interrupts;
    } else {
        // Unmask and enable interrupts
        regs->int_signal_en = error_interrupts | normal_interrupts;
        regs->int_status_en = error_interrupts | normal_interrupts;
    }

    dev->cmd_req = req;

    if (has_data || imx_sdmmc_cmd_rsp_busy(cmd)) {
        dev->data_req = req;
    } else {
        dev->data_req = NULL;
    }
    dev->data_blockid = 0;
    dev->data_done = false;

    // Start command
    regs->mix_ctrl &= ~(IMX_SDHC_MIX_CTRL_CMD_MASK);
    regs->mix_ctrl |= (cmd & IMX_SDHC_MIX_CTRL_CMD_MASK);
    regs->cmd_xfr_typ = (cmd & IMX_SDHC_CMD_XFER_TYPE_CMD_MASK);

#ifdef ENABLE_POLLING
    bool pio_done = false;

    while (!pio_done) {
        // wait for interrupt to occur
        while((regs->int_status & regs->int_status_en) == 0) {
            usleep(1);
        }

        // we got an interrupt. process it
        const uint32_t irq = regs->int_status;
        SDHCI_TRACE("(PIO MODE) got irq 0x%08x 0x%08x en 0x%08x sig 0x%08x, data_req %p\n",
            regs->int_status, irq, regs->int_status_en, regs->int_signal_en, dev->data_req);

        // Acknowledge the IRQs that we stashed.
        regs->int_status = irq;

        if (irq & error_interrupts) {
            SDHCI_ERROR("IRQ ERROR: 0x%x\n", irq);
            imx_decode_irq_error(irq);
            esdhc_dump(dev);
            if (irq & IMX_SDHC_INT_STAT_DMAE) {
                SDHCI_TRACE("ADMA error 0x%x ADMAADDR0 0x%x\n",
                regs->adma_err_status, regs->adma_sys_addr);
            }
            imx_sdhci_error_recovery_locked(dev);
        }

        if (irq & IMX_SDHC_INT_STAT_CC) {
            imx_sdhci_cmd_stage_complete_locked(dev);
            if (!has_data) {
                pio_done = true;
            }
        }
        if (irq & IMX_SDHC_INT_STAT_BRR) {
            if (dev->data_req->cmd_idx == MMC_SEND_TUNING_BLOCK) {
                pio_done = true;
            }
            imx_sdhci_data_stage_read_ready_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_BWR) {
            imx_sdhci_data_stage_write_ready_locked(dev);
        }
        if (irq & IMX_SDHC_INT_STAT_TC) {
            imx_sdhci_transfer_complete_locked(dev);
            pio_done = true;
        }
    }
#endif
    return ZX_OK;
}

static zx_status_t imx_sdhci_finish_req(imx_sdhci_device_t* dev, sdmmc_req_t* req) {
    zx_status_t status = ZX_OK;

    if (req->use_dma && req->pmt != ZX_HANDLE_INVALID) {
        /*
         * Clean the cache one more time after the DMA operation because there
         * might be a possibility of cpu prefetching while the DMA operation is
         * going on.
         */
        uint64_t req_len = req->blockcount * req->blocksize;
        if ((req->cmd_flags & SDMMC_CMD_READ) && req->use_dma) {
            status = zx_vmo_op_range(req->dma_vmo, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE,
                                             req->buf_offset, req_len, NULL, 0);
            if (status != ZX_OK) {
                zxlogf(ERROR, "aml-sd-emmc: cache clean failed with error  %d\n", status);
            }
        }

        status = zx_pmt_unpin(req->pmt);
        if (status != ZX_OK) {
            SDHCI_ERROR("error %d in pmt_unpin\n", status);
        }
        req->pmt = ZX_HANDLE_INVALID;
    }
    return status;
}

/* SDMMC PROTOCOL Implementations: host_info */
static zx_status_t imx_sdhci_host_info(void* ctx, sdmmc_host_info_t* info) {
    SDHCI_FUNC_ENTRY_LOG;
    imx_sdhci_device_t* dev = ctx;
    memcpy(info, &dev->info, sizeof(dev->info));
    return ZX_OK;
}

/* SDMMC PROTOCOL Implementations: set_signal_voltage */
static zx_status_t imx_sdhci_set_signal_voltage(void* ctx, sdmmc_voltage_t voltage) {
    SDHCI_FUNC_ENTRY_LOG;
    return ZX_OK; // TODO: Figure out how to change voltage using the regulator
}

/* SDMMC PROTOCOL Implementations: set_bus_width */
static zx_status_t imx_sdhci_set_bus_width(void* ctx, uint32_t bus_width) {
    SDHCI_FUNC_ENTRY_LOG;
    if (bus_width >= SDMMC_BUS_WIDTH_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = ZX_OK;
    imx_sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    if ((bus_width == SDMMC_BUS_WIDTH_8) && !(dev->info.caps & SDMMC_HOST_CAP_BUS_WIDTH_8)) {
        SDHCI_ERROR("8-bit bus width not supported\n");
        status = ZX_ERR_NOT_SUPPORTED;
        goto unlock;
    }

    switch (bus_width) {
        case SDMMC_BUS_WIDTH_1:
            dev->regs->prot_ctrl &= ~IMX_SDHC_PROT_CTRL_DTW_MASK;
            dev->regs->prot_ctrl |= IMX_SDHC_PROT_CTRL_DTW_1;
            break;
        case SDMMC_BUS_WIDTH_4:
            dev->regs->prot_ctrl &= ~IMX_SDHC_PROT_CTRL_DTW_MASK;
            dev->regs->prot_ctrl |= IMX_SDHC_PROT_CTRL_DTW_4;
            break;
        case SDMMC_BUS_WIDTH_8:
            dev->regs->prot_ctrl &= ~IMX_SDHC_PROT_CTRL_DTW_MASK;
            dev->regs->prot_ctrl |= IMX_SDHC_PROT_CTRL_DTW_8;
            break;
        default:
            break;
    }

    SDHCI_ERROR("set bus width to %d\n", bus_width);

unlock:
    mtx_unlock(&dev->mtx);
    return status;
}

/* SDMMC PROTOCOL Implementations: set_bus_freq */
static zx_status_t imx_sdhci_set_bus_freq(void* ctx, uint32_t bus_freq) {
    SDHCI_FUNC_ENTRY_LOG;
    zx_status_t status = ZX_OK;
    imx_sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    const uint32_t divider = get_clock_divider(dev, dev->base_clock, bus_freq);
    const uint8_t pre_div = (divider >> 16) & 0xFF;
    const uint8_t div = (divider & 0xF);

    SDHCI_TRACE("divider %d, pre_div %d, div = %d, ddr_mode %s\n",
        divider, pre_div, div, dev->ddr_mode? "ON" : "OFF");

    volatile struct imx_sdhci_regs* regs = dev->regs;

    uint32_t iterations = 0;
    while (regs->pres_state & (IMX_SDHC_PRES_STATE_CIHB | IMX_SDHC_PRES_STATE_CDIHB)) {
        if (++iterations > 1000) {
            status = ZX_ERR_TIMED_OUT;
            goto unlock;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    if(dev->ddr_mode) {
        regs->mix_ctrl |= IMX_SDHC_MIX_CTRL_DDR_EN;
    }

    regs->vend_spec &= ~(IMX_SDHC_VEND_SPEC_CARD_CLK_SOFT_EN);

    regs->sys_ctrl &=   ~(IMX_SDHC_SYS_CTRL_CLOCK_MASK);

    regs->sys_ctrl |=   (pre_div << IMX_SDHC_SYS_CTRL_PREDIV_SHIFT) |
                        (div << IMX_SDHC_SYS_CTRL_DIVIDER_SHIFT);

    // Add delay to make sure clocks are stable
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    regs->vend_spec |=  (IMX_SDHC_VEND_SPEC_IPG_PERCLK_SOFT_EN) |
                        (IMX_SDHC_VEND_SPEC_CARD_CLK_SOFT_EN);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    SDHCI_INFO("desired freq = %d, actual = %d, (%d, %d. %d)\n",
        bus_freq, dev->base_clock / (pre_div? dev->ddr_mode? pre_div<<2 : pre_div<<1 : dev->ddr_mode? 2: 1) / (div+1), dev->base_clock,
        pre_div, div);

unlock:
    mtx_unlock(&dev->mtx);
    return status;
}

static void imx_sdhci_set_strobe_dll(imx_sdhci_device_t* dev) {

    dev->regs->vend_spec &= ~(IMX_SDHC_VEND_SPEC_FRC_SDCLK_ON);
    dev->regs->dll_ctrl = IMX_SDHC_DLLCTRL_RESET;

    dev->regs->dll_ctrl = (IMX_SDHC_DLLCTRL_ENABLE | IMX_SDHC_DLLCTRL_SLV_DLY_TARGET);
    usleep(10);
    if(!(dev->regs->dll_status & IMX_SDHC_DLLSTS_REF_LOCK)) {
        SDHCI_ERROR("HS400 Strobe DLL status REF not locked!!\n");
    }
    if(!(dev->regs->dll_status & IMX_SDHC_DLLSTS_SLV_LOCK)) {
        SDHCI_ERROR("HS400 Strobe DLL status SLV not locked!!\n");
    }

}

/* SDMMC PROTOCOL Implementations: set_timing */
static zx_status_t imx_sdhci_set_timing(void* ctx, sdmmc_timing_t timing) {
    SDHCI_FUNC_ENTRY_LOG;
    if (timing >= SDMMC_TIMING_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = ZX_OK;
    imx_sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    uint32_t regVal = dev->regs->mix_ctrl;
    regVal &= ~(IMX_SDHC_MIX_CTRL_HS400 | IMX_SDHC_MIX_CTRL_DDR_EN);
    dev->ddr_mode = false;
    switch(timing) {
        case SDMMC_TIMING_LEGACY:
            mtx_unlock(&dev->mtx);
            imx_sdhci_set_bus_freq(dev, 25000000);
            mtx_lock(&dev->mtx);
            dev->regs->autocmd12_err_status &= ~(IMX_SDHC_AUTOCMD12_ERRSTS_SMP_CLK_SEL |
                                                    IMX_SDHC_AUTOCMD12_ERRSTS_EXE_TUNING);
            break;
        case SDMMC_TIMING_HS400:
            regVal |= (IMX_SDHC_MIX_CTRL_HS400 | IMX_SDHC_MIX_CTRL_DDR_EN);
            dev->regs->mix_ctrl = regVal;
            // make sure we are running at 200MHz already
            mtx_unlock(&dev->mtx);
            dev->ddr_mode = true;
            imx_sdhci_set_bus_freq(dev, 200000000);
            mtx_lock(&dev->mtx);
            imx_sdhci_set_strobe_dll(dev);
            break;
        case SDMMC_TIMING_HSDDR:
            dev->ddr_mode = true;
            regVal |= (IMX_SDHC_MIX_CTRL_DDR_EN);
            //fall through
        default:
            mtx_unlock(&dev->mtx);
            imx_sdhci_set_bus_freq(dev, 52000000);
            mtx_lock(&dev->mtx);
            dev->regs->mix_ctrl = regVal;
            break;
    }

    // need to upate pin state
    mtx_unlock(&dev->mtx);
    return status;
}

/* SDMMC PROTOCOL Implementations: hw_reset */
static void imx_sdhci_hw_reset(void* ctx) {
    SDHCI_FUNC_ENTRY_LOG;
    imx_sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    gpio_write(&dev->gpio, 0, 0);
    usleep(10000);
    gpio_write(&dev->gpio, 0, 1);

    dev->info.caps |= SDMMC_HOST_CAP_AUTO_CMD12;

    // Reset host controller
    dev->regs->sys_ctrl |= IMX_SDHC_SYS_CTRL_RSTA;
    if (imx_sdhci_wait_for_reset(dev, IMX_SDHC_SYS_CTRL_RSTA, ZX_SEC(1)) != ZX_OK) {
        SDHCI_ERROR("Did not recover from reset 0x%x\n", dev->regs->sys_ctrl);
        mtx_unlock(&dev->mtx);
        return;
    }

    dev->regs->mmc_boot = 0;
    dev->regs->mix_ctrl = 0;
    dev->regs->clk_tune_ctrl_status = 0;
    dev->regs->dll_ctrl = 0;
    dev->regs->autocmd12_err_status = 0;
    dev->regs->vend_spec = IMX_SDHC_VEND_SPEC_INIT;
    dev->regs->vend_spec |= IMX_SDHC_VEND_SPEC_HCLK_SOFT_EN | IMX_SDHC_VEND_SPEC_IPG_CLK_SOFT_EN;
    dev->regs->sys_ctrl &= ~(IMX_SDHC_SYS_CTRL_DTOCV_MASK);
    dev->regs->sys_ctrl |= (IMX_SDHC_SYS_CTRL_DTOCV(0xe));
    dev->regs->prot_ctrl = IMX_SDHC_PROT_CTRL_INIT;

    uint32_t regVal = dev->regs->tuning_ctrl;
    regVal &= ~(IMX_SDHC_TUNING_CTRL_START_TAP_MASK);
    regVal &= ~(IMX_SDHC_TUNING_CTRL_STEP_MASK);
    regVal &= ~(IMX_SDHC_TUNING_CTRL_STD_TUN_EN);
    regVal |=   (IMX_SDHC_TUNING_CTRL_START_TAP(20)) |
                (IMX_SDHC_TUNING_CTRL_STEP(2)) |
                (IMX_SDHC_TUNING_CTRL_STD_TUN_EN);
    dev->regs->tuning_ctrl = regVal;

    dev->regs->vend_spec |= (1 << 1);
    usleep(100);

    // enable clocks
    mtx_unlock(&dev->mtx);
    imx_sdhci_set_bus_freq(dev, SD_FREQ_SETUP_HZ);
    imx_sdhci_set_bus_width(dev, SDMMC_BUS_WIDTH_1);
}

/* SDMMC PROTOCOL Implementations: request */
static zx_status_t imx_sdhci_request(void* ctx, sdmmc_req_t* req) {
    SDHCI_FUNC_ENTRY_LOG;
    zx_status_t status = ZX_OK;
    imx_sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);
    // one command at a time
    if ((dev->cmd_req != NULL) || (dev->data_req != NULL)) {
        status = ZX_ERR_SHOULD_WAIT;
        goto unlock_out;
    }


    status = imx_sdhci_start_req_locked(dev, req);
    if (status != ZX_OK) {
        goto unlock_out;
    }

    mtx_unlock(&dev->mtx);

    completion_wait(&dev->req_completion, ZX_TIME_INFINITE);

    imx_sdhci_finish_req(dev, req);

    completion_reset(&dev->req_completion);

    return req->status;

unlock_out:
    mtx_unlock(&dev->mtx);
    imx_sdhci_finish_req(dev, req);
    return status;
}

/* SDMMC PROTOCOL Implementations: perform_tuning */
static zx_status_t imx_sdhci_perform_tuning(void* ctx) {
    SDHCI_FUNC_ENTRY_LOG;
    imx_sdhci_device_t* dev = ctx;
    uint32_t regVal;

    mtx_lock(&dev->mtx);

    sdmmc_req_t req = {
        .cmd_idx = MMC_SEND_TUNING_BLOCK,
        .cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS,
        .arg = 0,
        .blockcount = 0,
        .blocksize = (dev->regs->prot_ctrl & IMX_SDHC_PROT_CTRL_DTW_8) ? 128 : 64,
    };

    // Setup Standard Tuning
    regVal = dev->regs->autocmd12_err_status;
    regVal &= ~(IMX_SDHC_AUTOCMD12_ERRSTS_SMP_CLK_SEL);
    regVal |= IMX_SDHC_AUTOCMD12_ERRSTS_EXE_TUNING;
    dev->regs->autocmd12_err_status = regVal;

    regVal = dev->regs->mix_ctrl;
    regVal &= ~(IMX_SDHC_MIX_CTRL_FBCLK_SEL | IMX_SDHC_MIX_CTRL_AUTO_TUNE);
    regVal |= (IMX_SDHC_MIX_CTRL_FBCLK_SEL | IMX_SDHC_MIX_CTRL_AUTO_TUNE);
    dev->regs->mix_ctrl = regVal;

    int count = 0;
    do {
        mtx_unlock(&dev->mtx);
        usleep(1000);
        zx_status_t st = imx_sdhci_request(dev, &req);
        if (st != ZX_OK) {
            SDHCI_ERROR("sdhci: MMC_SEND_TUNING_BLOCK error, retcode = %d\n", req.status);
            return st;
        }
        mtx_lock(&dev->mtx);
    } while (   ((dev->regs->autocmd12_err_status & IMX_SDHC_AUTOCMD12_ERRSTS_EXE_TUNING)) &&
                count++ < (MAX_TUNING_COUNT));

    bool fail = (dev->regs->autocmd12_err_status & IMX_SDHC_AUTOCMD12_ERRSTS_EXE_TUNING) ||
                !(dev->regs->autocmd12_err_status & IMX_SDHC_AUTOCMD12_ERRSTS_SMP_CLK_SEL);

    // Give the card some time to finish up
    usleep(1000);
    mtx_unlock(&dev->mtx);

    SDHCI_ERROR("sdhci: tuning %s\n", fail? "failed!":"successful!");

    if (fail) {
        esdhc_dump(dev);
        return ZX_ERR_IO;
    }
   return ZX_OK;
}

static zx_status_t imx_sdhci_get_oob_irq(void* ctx, zx_handle_t *oob_irq_handle) {
    // Currently we do not support SDIO
    return ZX_ERR_NOT_SUPPORTED;
}

static sdmmc_protocol_ops_t sdmmc_proto = {
    .host_info = imx_sdhci_host_info,
    .set_signal_voltage = imx_sdhci_set_signal_voltage,
    .set_bus_width = imx_sdhci_set_bus_width,
    .set_bus_freq = imx_sdhci_set_bus_freq,
    .set_timing = imx_sdhci_set_timing,
    .hw_reset = imx_sdhci_hw_reset,
    .perform_tuning = imx_sdhci_perform_tuning,
    .request = imx_sdhci_request,
    .get_sdio_oob_irq = imx_sdhci_get_oob_irq,
};

static void imx_sdhci_unbind(void* ctx) {
    imx_sdhci_device_t* dev = ctx;
    device_remove(dev->zxdev);
}

static void imx_sdhci_release(void* ctx) {
    imx_sdhci_device_t* dev = ctx;
    if (dev->regs != NULL) {
        zx_handle_close(dev->regs_handle);
    }
    zx_handle_close(dev->bti_handle);
    free(dev);
}

static zx_protocol_device_t imx_sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = imx_sdhci_unbind,
    .release = imx_sdhci_release,

};

static zx_status_t imx_sdhci_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    imx_sdhci_device_t* dev = calloc(1, sizeof(imx_sdhci_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &dev->pdev);
    if (status != ZX_OK) {
        SDHCI_ERROR("ZX_PROTOCOL_PLATFORM_DEV not available %d \n", status);
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_GPIO, &dev->gpio);
    if (status != ZX_OK) {
        SDHCI_ERROR("ZX_PROTOCOL_GPIO not available %d\n", status);
        goto fail;
    }

    status = pdev_map_mmio_buffer(&dev->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                    &dev->mmios);
    if (status != ZX_OK) {
        SDHCI_ERROR("pdev_map_mmio_buffer failed %d\n", status);
        goto fail;
    }

    // hook up mmio to dev->regs
    dev->regs = io_buffer_virt(&dev->mmios);

    status = pdev_get_bti(&dev->pdev, 0, &dev->bti_handle);
    if (status != ZX_OK) {
        SDHCI_ERROR("Could not get BTI handle %d\n", status);
        goto fail;
    }

    status = pdev_map_interrupt(&dev->pdev, 0, &dev->irq_handle);
    if (status != ZX_OK) {
        SDHCI_ERROR("pdev_map_interrupt failed %d\n", status);
        goto fail;
    }

#ifndef ENABLE_POLLING
    thrd_t irq_thread;
    if (thrd_create_with_name(&irq_thread, imx_sdhci_irq_thread,
                                        dev, "imx_sdhci_irq_thread") != thrd_success) {
        SDHCI_ERROR("Failed to create irq thread\n");
    }
    thrd_detach(irq_thread);
#endif

    dev->base_clock = IMX8M_SDHCI_BASE_CLOCK; // TODO: Better way of obtaining this info

    // Toggle the reset button
    if (gpio_config(&dev->gpio, 0, GPIO_DIR_OUT) != ZX_OK) {
        SDHCI_ERROR("Could not configure RESET pin as output\n");
        goto fail;
    }

    uint32_t caps0 = dev->regs->host_ctrl_cap;

    //TODO: Turn off 8-bit mode for now since it doesn't work
    dev->info.caps |= SDMMC_HOST_CAP_BUS_WIDTH_8;
#ifndef DISABLE_DMA
    dev->info.caps |= SDMMC_HOST_CAP_ADMA2;
#endif
    if (caps0 & SDHCI_CORECFG_3P3_VOLT_SUPPORT) {
        dev->info.caps |= SDMMC_HOST_CAP_VOLTAGE_330;
    }

    dev->info.caps |= SDMMC_HOST_CAP_AUTO_CMD12;

    // TODO: Disable HS400 for now
    dev->info.prefs |= SDMMC_HOST_PREFS_DISABLE_HS400;
#ifndef DISABLE_DMA
    status = io_buffer_init(&dev->iobuf, dev->bti_handle,
                            DMA_DESC_COUNT * sizeof(sdhci_adma64_desc_t),
                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        SDHCI_ERROR("Could not allocate DMA buffer. Falling to PIO Mode\n");
        dev->dma_mode = false;
        dev->info.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
    } else {
        SDHCI_ERROR("0x%lx %p\n", io_buffer_phys(&dev->iobuf), io_buffer_virt(&dev->iobuf));
        dev->descs = io_buffer_virt(&dev->iobuf);
        dev->info.max_transfer_size = DMA_DESC_COUNT * PAGE_SIZE;
        dev->regs->prot_ctrl &= ~(IMX_SDHC_PROT_CTRL_DMASEL_MASK);
        dev->regs->prot_ctrl |= IMX_SDHC_PROT_CTRL_DMASEL_ADMA2;
        dev->dma_mode = true;
        SDHCI_ERROR("Enabling DMA Mode\n");
    }
#else
        SDHCI_ERROR("DMA Mode Disabled. Using PIO Mode\n");
        dev->dma_mode = false;
        dev->info.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
#endif

    // Disable all interrupts
    dev->regs->int_signal_en = 0;
    dev->regs->int_status = 0xffffffff;

#ifdef ENABLE_POLLING
    SDHCI_INFO("Interrupts Disabled! Polling Mode Active\n");
#else
    SDHCI_INFO("Interrupts Enabled\n");
#endif

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "imx-sdhci",
        .ctx = dev,
        .ops = &imx_sdhci_device_proto,
        .proto_id = ZX_PROTOCOL_SDMMC,
        .proto_ops = &sdmmc_proto,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        SDHCI_ERROR("device_add failed %d\n", status);
        goto fail;
    }

    return ZX_OK;

fail:
    imx_sdhci_release(dev);
    return status;
}

static zx_driver_ops_t imx_sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = imx_sdhci_bind,
};

ZIRCON_DRIVER_BEGIN(imx_sdhci, imx_sdhci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_NXP),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_IMX_SDHCI),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_IMX8MEVK),
ZIRCON_DRIVER_END(imx_sdhci)
