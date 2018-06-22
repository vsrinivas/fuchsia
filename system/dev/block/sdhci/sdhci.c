// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes and limitations:
// 1. This driver only uses PIO mode.
//
// 2. This driver only supports SDHCv3 and above. Lower versions of SD are not
//    currently supported. The driver should fail gracefully if a lower version
//    card is detected.

// Standard Includes
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdhci.h>
#include <hw/sdmmc.h>

// Zircon Includes
#include <zircon/threads.h>
#include <zircon/assert.h>
#include <sync/completion.h>
#include <pretty/hexdump.h>

#define SD_FREQ_SETUP_HZ  400000

#define MAX_TUNING_COUNT 40

#define PAGE_MASK   (PAGE_SIZE - 1ull)

#define HI32(val)   (((val) >> 32) & 0xffffffff)
#define LO32(val)   ((val) & 0xffffffff)
#define SDHCI_CMD_IDX(c) ((c) << 24)

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
    uint64_t address;
} __PACKED sdhci_adma64_desc_t;

static_assert(sizeof(sdhci_adma64_desc_t) == 12, "unexpected ADMA2 descriptor size");

// 64k max per descriptor
#define ADMA2_DESC_MAX_LENGTH   0x10000 // 64k
// for 2M max transfer size for fully discontiguous
// also see SDMMC_PAGES_COUNT in ddk/protocol/sdmmc.h
#define DMA_DESC_COUNT          512

typedef struct sdhci_device {
    zx_device_t* zxdev;

    zx_handle_t irq_handle;
    thrd_t irq_thread;

    volatile sdhci_regs_t* regs;

    sdhci_protocol_t sdhci;

    zx_handle_t bti_handle;

    // DMA descriptors
    io_buffer_t iobuf;
    sdhci_adma64_desc_t* descs;

    // Held when a command or action is in progress.
    mtx_t mtx;

    // Current command request
    sdmmc_req_t* cmd_req;
    // Current data line request
    sdmmc_req_t* data_req;
    // Current block id to transfer (PIO)
    uint16_t data_blockid;
    uint16_t reserved;
    // Set to true if the data stage completed before the command stage
    bool data_done;
    // used to signal request complete
    completion_t req_completion;

    // Controller info
    sdmmc_host_info_t info;

    // Controller specific quirks
    uint64_t quirks;

    // Base clock rate
    uint32_t base_clock;
} sdhci_device_t;

// If any of these interrupts is asserted in the SDHCI irq register, it means
// that an error has occured.
static const uint32_t error_interrupts = (
    SDHCI_IRQ_ERR |
    SDHCI_IRQ_ERR_CMD_TIMEOUT |
    SDHCI_IRQ_ERR_CMD_CRC |
    SDHCI_IRQ_ERR_CMD_END_BIT |
    SDHCI_IRQ_ERR_CMD_INDEX |
    SDHCI_IRQ_ERR_DAT_TIMEOUT |
    SDHCI_IRQ_ERR_DAT_CRC |
    SDHCI_IRQ_ERR_DAT_ENDBIT |
    SDHCI_IRQ_ERR_CURRENT_LIMIT |
    SDHCI_IRQ_ERR_AUTO_CMD |
    SDHCI_IRQ_ERR_ADMA |
    SDHCI_IRQ_ERR_TUNING
);

// These interrupts indicate that a transfer or command has progressed normally.
static const uint32_t normal_interrupts = (
    SDHCI_IRQ_CMD_CPLT |
    SDHCI_IRQ_XFER_CPLT |
    SDHCI_IRQ_BUFF_READ_READY |
    SDHCI_IRQ_BUFF_WRITE_READY
);

static bool sdmmc_cmd_rsp_busy(uint32_t cmd_flags) {
    return cmd_flags & SDMMC_RESP_LEN_48B;
}

static bool sdmmc_cmd_has_data(uint32_t cmd_flags) {
    return cmd_flags & SDMMC_RESP_DATA_PRESENT;
}

static bool sdhci_supports_adma2_64bit(sdhci_device_t* dev) {
    return (dev->info.caps & SDMMC_HOST_CAP_ADMA2) &&
           (dev->info.caps & SDMMC_HOST_CAP_64BIT) &&
           !(dev->quirks & SDHCI_QUIRK_NO_DMA);
}

static uint32_t sdhci_prepare_cmd(sdmmc_req_t* req) {
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

static zx_status_t sdhci_wait_for_reset(sdhci_device_t* dev, const uint32_t mask, zx_time_t timeout) {
    zx_time_t deadline = zx_clock_get(ZX_CLOCK_MONOTONIC) + timeout;
    while (true) {
        if (((dev->regs->ctrl1) & mask) == 0) {
            break;
        }
        if (zx_clock_get(ZX_CLOCK_MONOTONIC) > deadline) {
            printf("sdhci: timed out while waiting for reset\n");
            return ZX_ERR_TIMED_OUT;
        }
    }
    return ZX_OK;
}

static void sdhci_complete_request_locked(sdhci_device_t* dev, sdmmc_req_t* req,
                                          zx_status_t status) {
    zxlogf(TRACE, "sdhci: complete cmd 0x%08x status %d\n", req->cmd_idx, status);

    // Disable irqs when no pending transfer
    dev->regs->irqen = 0;

    dev->cmd_req = NULL;
    dev->data_req = NULL;
    dev->data_blockid = 0;
    dev->data_done = false;

    req->status = status;
    completion_signal(&dev->req_completion);
}

static void sdhci_cmd_stage_complete_locked(sdhci_device_t* dev) {
    zxlogf(TRACE, "sdhci: got CMD_CPLT interrupt\n");

    if (!dev->cmd_req) {
        zxlogf(TRACE, "sdhci: spurious CMD_CPLT interrupt!\n");
        return;
    }

    sdmmc_req_t* req = dev->cmd_req;
    volatile struct sdhci_regs* regs = dev->regs;
    uint32_t cmd = sdhci_prepare_cmd(req);

    // Read the response data.
    if (cmd & SDHCI_CMD_RESP_LEN_136) {
        if (dev->quirks & SDHCI_QUIRK_STRIP_RESPONSE_CRC) {
            req->response[0] = (regs->resp3 << 8) | ((regs->resp2 >> 24) & 0xFF);
            req->response[1] = (regs->resp2 << 8) | ((regs->resp1 >> 24) & 0xFF);
            req->response[2] = (regs->resp1 << 8) | ((regs->resp0 >> 24) & 0xFF);
            req->response[3] = (regs->resp0 << 8);
        } else if (dev->quirks & SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER) {
            req->response[0] = (regs->resp0 << 8);
            req->response[1] = (regs->resp1 << 8) | ((regs->resp0 >> 24) & 0xFF);
            req->response[2] = (regs->resp2 << 8) | ((regs->resp1 >> 24) & 0xFF);
            req->response[3] = (regs->resp3 << 8) | ((regs->resp2 >> 24) & 0xFF);
        } else {
            req->response[0] = regs->resp0;
            req->response[1] = regs->resp1;
            req->response[2] = regs->resp2;
            req->response[3] = regs->resp3;
        }
    } else if (cmd & (SDHCI_CMD_RESP_LEN_48 | SDHCI_CMD_RESP_LEN_48B)) {
        req->response[0] = regs->resp0;
        req->response[1] = regs->resp1;
    }

    // We're done if the command has no data stage or if the data stage completed early
    if (!dev->data_req || dev->data_done) {
        sdhci_complete_request_locked(dev, dev->cmd_req, ZX_OK);
    } else {
        dev->cmd_req = NULL;
    }
}

static void sdhci_data_stage_read_ready_locked(sdhci_device_t* dev) {
    zxlogf(TRACE, "sdhci: got BUFF_READ_READY interrupt\n");

    if (!dev->data_req || !sdmmc_cmd_has_data(dev->data_req->cmd_flags)) {
        zxlogf(TRACE, "sdhci: spurious BUFF_READ_READY interrupt!\n");
        return;
    }

    sdmmc_req_t* req = dev->data_req;

    if (dev->data_req->cmd_idx == MMC_SEND_TUNING_BLOCK) {
        // tuning command is done here
        sdhci_complete_request_locked(dev, dev->data_req, ZX_OK);
    } else {
        // Sequentially read each block.
        for (size_t byteid = 0; byteid < req->blocksize; byteid += 4) {
            const size_t offset = dev->data_blockid * req->blocksize + byteid;
            uint32_t* wrd = req->virt + offset;
            *wrd = dev->regs->data;
        }
        dev->data_blockid += 1;
    }
}

static void sdhci_data_stage_write_ready_locked(sdhci_device_t* dev) {
    zxlogf(TRACE, "sdhci: got BUFF_WRITE_READY interrupt\n");

    if (!dev->data_req || !sdmmc_cmd_has_data(dev->data_req->cmd_flags)) {
        zxlogf(TRACE, "sdhci: spurious BUFF_WRITE_READY interrupt!\n");
        return;
    }

    sdmmc_req_t* req = dev->data_req;

    // Sequentially write each block.
    for (size_t byteid = 0; byteid < req->blocksize; byteid += 4) {
        const size_t offset = dev->data_blockid * req->blocksize + byteid;
        uint32_t* wrd = req->virt + offset;
        dev->regs->data = *wrd;
    }
    dev->data_blockid += 1;
}

static void sdhci_transfer_complete_locked(sdhci_device_t* dev) {
    zxlogf(TRACE, "sdhci: got XFER_CPLT interrupt\n");
    if (!dev->data_req) {
        zxlogf(TRACE, "sdhci: spurious XFER_CPLT interrupt!\n");
        return;
    }
    if (dev->cmd_req) {
        dev->data_done = true;
    } else {
        sdhci_complete_request_locked(dev, dev->data_req, ZX_OK);
    }
}

static void sdhci_error_recovery_locked(sdhci_device_t* dev) {
    // Reset internal state machines
    dev->regs->ctrl1 |= SDHCI_SOFTWARE_RESET_CMD;
    sdhci_wait_for_reset(dev, SDHCI_SOFTWARE_RESET_CMD, ZX_SEC(1));
    dev->regs->ctrl1 |= SDHCI_SOFTWARE_RESET_DAT;
    sdhci_wait_for_reset(dev, SDHCI_SOFTWARE_RESET_DAT, ZX_SEC(1));

    // TODO data stage abort

    // Complete any pending txn with error status
    if (dev->cmd_req != NULL) {
        sdhci_complete_request_locked(dev, dev->cmd_req, ZX_ERR_IO);
    } else if (dev->data_req != NULL) {
        sdhci_complete_request_locked(dev, dev->data_req, ZX_ERR_IO);
    }
}

static uint32_t get_clock_divider(const uint32_t base_clock,
                                  const uint32_t target_rate) {
    if (target_rate >= base_clock) {
        // A clock divider of 0 means "don't divide the clock"
        // If the base clock is already slow enough to use as the SD clock then
        // we don't need to divide it any further.
        return 0;
    }

    uint32_t result = base_clock / (2 * target_rate);
    if (result * target_rate * 2 < base_clock)
        result++;

    return result;
}

static int sdhci_irq_thread(void *arg) {
    zx_status_t wait_res;
    sdhci_device_t* dev = (sdhci_device_t*)arg;
    volatile struct sdhci_regs* regs = dev->regs;
    zx_handle_t irq_handle = dev->irq_handle;

    while (true) {
        wait_res = zx_interrupt_wait(irq_handle, NULL);
        if (wait_res != ZX_OK) {
            if (wait_res != ZX_ERR_CANCELED) {
                zxlogf(ERROR, "sdhci: interrupt wait failed with retcode = %d\n", wait_res);
            }
            break;
        }

        const uint32_t irq = regs->irq;
        zxlogf(TRACE, "got irq 0x%08x 0x%08x en 0x%08x\n", regs->irq, irq, regs->irqen);

        // Acknowledge the IRQs that we stashed. IRQs are cleared by writing
        // 1s into the IRQs that fired.
        regs->irq = irq;

        mtx_lock(&dev->mtx);
        if (irq & SDHCI_IRQ_CMD_CPLT) {
            sdhci_cmd_stage_complete_locked(dev);
        }
        if (irq & SDHCI_IRQ_BUFF_READ_READY) {
            sdhci_data_stage_read_ready_locked(dev);
        }
        if (irq & SDHCI_IRQ_BUFF_WRITE_READY) {
            sdhci_data_stage_write_ready_locked(dev);
        }
        if (irq & SDHCI_IRQ_XFER_CPLT) {
            sdhci_transfer_complete_locked(dev);
        }
        if (irq & error_interrupts) {
            if (driver_get_log_flags() & DDK_LOG_TRACE) {
                if (irq & SDHCI_IRQ_ERR_ADMA) {
                    zxlogf(TRACE, "sdhci: ADMA error 0x%x ADMAADDR0 0x%x ADMAADDR1 0x%x\n",
                            regs->admaerr, regs->admaaddr0, regs->admaaddr1);
                }
            }
            sdhci_error_recovery_locked(dev);
        }
        mtx_unlock(&dev->mtx);
    }
    return 0;
}

static zx_status_t sdhci_build_dma_desc(sdhci_device_t* dev, sdmmc_req_t* req) {
    block_op_t* bop = &req->txn->bop;
    uint64_t pagecount = ((bop->rw.offset_vmo & PAGE_MASK) + bop->rw.length + PAGE_MASK) /
                         PAGE_SIZE;
    if (pagecount > SDMMC_PAGES_COUNT) {
        zxlogf(ERROR, "sdhci: too many pages %lu vs %lu\n", pagecount, SDMMC_PAGES_COUNT);
        return ZX_ERR_INVALID_ARGS;
    }

    // pin the vmo
    zx_paddr_t phys[SDMMC_PAGES_COUNT];
    zx_handle_t pmt;
    // offset_vmo is converted to bytes by the sdmmc layer
    uint32_t options = bop->command == BLOCK_OP_READ ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;
    zx_status_t st = zx_bti_pin(dev->bti_handle, options, bop->rw.vmo,
                                bop->rw.offset_vmo & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, phys, pagecount, &pmt);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdhci: error %d bti_pin\n", st);
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
        zxlogf(ERROR, "sdhci: cache clean failed with error  %d\n", st);
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
                zxlogf(TRACE, "sdhci: empty descriptor list!\n");
                return ZX_ERR_NOT_SUPPORTED;
            }
        } else if (length > ADMA2_DESC_MAX_LENGTH) {
            zxlogf(TRACE, "sdhci: chunk size > %zu is unsupported\n", length);
            return ZX_ERR_NOT_SUPPORTED;
        } else if ((++count) > DMA_DESC_COUNT) {
            zxlogf(TRACE, "sdhci: request with more than %zd chunks is unsupported\n",
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
            zxlogf(SPEW, "desc: addr=0x%" PRIx64 " length=0x%04x attr=0x%04x\n",
                    desc->address, desc->length, desc->attr);
        } while (!(desc++)->end);
    }
    return ZX_OK;
}

static zx_status_t sdhci_start_req_locked(sdhci_device_t* dev, sdmmc_req_t* req) {

    volatile struct sdhci_regs* regs = dev->regs;
    const uint32_t arg = req->arg;
    const uint16_t blkcnt = req->blockcount;
    const uint16_t blksiz = req->blocksize;
    uint32_t cmd = sdhci_prepare_cmd(req);
    bool has_data = sdmmc_cmd_has_data(req->cmd_flags);

    if (req->use_dma && !sdhci_supports_adma2_64bit(dev)) {
        zxlogf(TRACE, "sdhci: host does not support DMA\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    zxlogf(TRACE, "sdhci: start_req cmd=0x%08x (data %d dma %d bsy %d) blkcnt %u blksiz %u\n",
                  cmd, has_data, req->use_dma, sdmmc_cmd_rsp_busy(req->cmd_flags), blkcnt, blksiz);

    // Every command requires that the Command Inhibit is unset.
    uint32_t inhibit_mask = SDHCI_STATE_CMD_INHIBIT;

    // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
    // it's an abort command which can be issued with the data lines active.
    if (((cmd & SDHCI_CMD_RESP_LEN_48B) == SDHCI_CMD_RESP_LEN_48B) &&
        ((cmd & SDHCI_CMD_TYPE_ABORT) == 0)) {
        inhibit_mask |= SDHCI_STATE_DAT_INHIBIT;
    }

    // Wait for the inhibit masks from above to become 0 before issuing the command.
    while (regs->state & inhibit_mask) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    zx_status_t st = ZX_OK;
    if (has_data) {
        if (req->use_dma) {
            st = sdhci_build_dma_desc(dev, req);
            if (st != ZX_OK) {
                goto err;
            }

            zx_paddr_t desc_phys = io_buffer_phys(&dev->iobuf);
            dev->regs->admaaddr0 = LO32(desc_phys);
            dev->regs->admaaddr1 = HI32(desc_phys);

            zxlogf(SPEW, "sdhci: descs at 0x%x 0x%x\n",
                    dev->regs->admaaddr0, dev->regs->admaaddr1);

            cmd |= SDHCI_XFERMODE_DMA_ENABLE;
        }

        if (cmd & SDHCI_CMD_MULTI_BLK) {
            cmd |= SDHCI_CMD_AUTO12;
        }
    }

    regs->blkcntsiz = (blksiz | (blkcnt << 16));

    regs->arg1 = arg;

    // Clear any pending interrupts before starting the transaction.
    regs->irq = regs->irqen;

    // Unmask and enable interrupts
    regs->irqen = error_interrupts | normal_interrupts;
    regs->irqmsk = error_interrupts | normal_interrupts;

    // Start command
    regs->cmd = cmd;

    dev->cmd_req = req;
    if (has_data || sdmmc_cmd_rsp_busy(req->cmd_flags)) {
        dev->data_req = req;
    } else {
        dev->data_req = NULL;
    }
    dev->data_blockid = 0;
    dev->data_done = false;
    return ZX_OK;
err:
    return st;
}

static zx_status_t sdhci_finish_req(sdhci_device_t* dev, sdmmc_req_t* req) {
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
                zxlogf(ERROR, "sdhci: cache clean failed with error  %d\n", st);
            }
        }
        st = zx_pmt_unpin(req->pmt);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdhci: error %d in pmt_unpin\n", st);
        }
        req->pmt = ZX_HANDLE_INVALID;
    }
    return st;
}

static zx_status_t sdhci_host_info(void* ctx, sdmmc_host_info_t* info) {
    sdhci_device_t* dev = ctx;
    memcpy(info, &dev->info, sizeof(dev->info));
    return ZX_OK;
}

static zx_status_t sdhci_set_signal_voltage(void* ctx, sdmmc_voltage_t voltage) {
    if (voltage >= SDMMC_VOLTAGE_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t st = ZX_OK;
    sdhci_device_t* dev = ctx;
    volatile struct sdhci_regs* regs = dev->regs;

    mtx_lock(&dev->mtx);

    // Validate the controller supports the requested voltage
    if ((voltage == SDMMC_VOLTAGE_330) && !(dev->info.caps & SDMMC_HOST_CAP_VOLTAGE_330)) {
        zxlogf(TRACE, "sdhci: 3.3V signal voltage not supported\n");
        st = ZX_ERR_NOT_SUPPORTED;
        goto unlock;
    }

    // Disable the SD clock before messing with the voltage.
    regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    switch (voltage) {
    case SDMMC_VOLTAGE_180: {
        regs->ctrl2 |= SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
        // 1.8V regulator out should be stable within 5ms
        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
        if (driver_get_log_flags() & DDK_LOG_TRACE) {
            if (!(regs->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA)) {
                zxlogf(TRACE, "sdhci: 1.8V regulator output did not become stable\n");
                st = ZX_ERR_INTERNAL;
                goto unlock;
            }
        }
        break;
    }
    case SDMMC_VOLTAGE_330: {
        regs->ctrl2 &= ~SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
        // 3.3V regulator out should be stable within 5ms
        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
        if (driver_get_log_flags() & DDK_LOG_TRACE) {
            if (regs->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA) {
                zxlogf(TRACE, "sdhci: 3.3V regulator output did not become stable\n");
                st = ZX_ERR_INTERNAL;
                goto unlock;
            }
        }
        break;
    }
    default:
        break;
    }

    // Make sure our changes are acknolwedged.
    uint32_t expected_mask = SDHCI_PWRCTRL_SD_BUS_POWER;
    switch (voltage) {
    case SDMMC_VOLTAGE_180:
        expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
        break;
    case SDMMC_VOLTAGE_330:
        expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
        break;
    default:
        break;
    }
    if ((regs->ctrl0 & expected_mask) != expected_mask) {
        zxlogf(TRACE, "sdhci: after voltage switch ctrl0=0x%08x, expected=0x%08x\n",
               regs->ctrl0, expected_mask);
        st = ZX_ERR_INTERNAL;
        goto unlock;
    }

    // Turn the clock back on
    regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    zxlogf(TRACE, "sdhci: switch signal voltage to %d\n", voltage);

unlock:
    mtx_unlock(&dev->mtx);
    return st;
}

static zx_status_t sdhci_set_bus_width(void* ctx, uint32_t bus_width) {
    if (bus_width >= SDMMC_BUS_WIDTH_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t st = ZX_OK;
    sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    if ((bus_width == SDMMC_BUS_WIDTH_8) && !(dev->info.caps & SDMMC_HOST_CAP_BUS_WIDTH_8)) {
        zxlogf(TRACE, "sdhci: 8-bit bus width not supported\n");
        st =  ZX_ERR_NOT_SUPPORTED;
        goto unlock;
    }

    switch (bus_width) {
    case SDMMC_BUS_WIDTH_1:
        dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
        dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
        break;
    case SDMMC_BUS_WIDTH_4:
        dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
        break;
    case SDMMC_BUS_WIDTH_8:
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_EXT_DATA_WIDTH;
        break;
    default:
        break;
    }

    zxlogf(TRACE, "sdhci: set bus width to %d\n", bus_width);

unlock:
    mtx_unlock(&dev->mtx);
    return st;
}

static zx_status_t sdhci_set_bus_freq(void* ctx, uint32_t bus_freq) {
    zx_status_t st = ZX_OK;
    sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    const uint32_t divider = get_clock_divider(dev->base_clock, bus_freq);
    const uint8_t divider_lo = divider & 0xff;
    const uint8_t divider_hi = (divider >> 8) & 0x3;

    volatile struct sdhci_regs* regs = dev->regs;

    uint32_t iterations = 0;
    while (regs->state & (SDHCI_STATE_CMD_INHIBIT | SDHCI_STATE_DAT_INHIBIT)) {
        if (++iterations > 1000) {
            st = ZX_ERR_TIMED_OUT;
            goto unlock;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    // Turn off the SD clock before messing with the clock rate.
    regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    // Write the new divider into the control register.
    uint32_t ctrl1 = regs->ctrl1;
    ctrl1 &= ~0xffe0;
    ctrl1 |= ((divider_lo << 8) | (divider_hi << 6));
    regs->ctrl1 = ctrl1;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    // Turn the SD clock back on.
    regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    zxlogf(TRACE, "sdhci: set bus frequency to %u\n", bus_freq);

unlock:
    mtx_unlock(&dev->mtx);
    return st;
}

static zx_status_t sdhci_set_timing(void* ctx, sdmmc_timing_t timing) {
    if (timing >= SDMMC_TIMING_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t st = ZX_OK;
    sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    // Toggle high-speed
    if (timing != SDMMC_TIMING_LEGACY) {
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
    } else {
        dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
    }

    // Disable SD clock before changing UHS timing
    dev->regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    uint32_t ctrl2 = dev->regs->ctrl2 & ~SDHCI_HOSTCTRL2_UHS_MODE_SELECT_MASK;
    if (timing == SDMMC_TIMING_HS200) {
        ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR104;
    } else if (timing == SDMMC_TIMING_HS400) {
        ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_HS400;
    } else if (timing == SDMMC_TIMING_HSDDR) {
        ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_DDR50;
    }
    dev->regs->ctrl2 = ctrl2;

    // Turn the SD clock back on.
    dev->regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    zxlogf(TRACE, "sdhci: set bus timing to %d\n", timing);

    mtx_unlock(&dev->mtx);
    return st;
}

static void sdhci_hw_reset(void* ctx) {
    sdhci_device_t* dev = ctx;
    mtx_lock(&dev->mtx);
    if (dev->sdhci.ops->hw_reset) {
        dev->sdhci.ops->hw_reset(dev->sdhci.ctx);
    }
    mtx_unlock(&dev->mtx);
}

static zx_status_t sdhci_request(void* ctx, sdmmc_req_t* req) {
    zx_status_t st = ZX_OK;
    sdhci_device_t* dev = ctx;

    mtx_lock(&dev->mtx);

    // one command at a time
    if ((dev->cmd_req != NULL) || (dev->data_req != NULL)) {
        st = ZX_ERR_SHOULD_WAIT;
        goto unlock_out;
    }

    st = sdhci_start_req_locked(dev, req);
    if (st != ZX_OK) {
        goto unlock_out;
    }

    mtx_unlock(&dev->mtx);

    completion_wait(&dev->req_completion, ZX_TIME_INFINITE);

    sdhci_finish_req(dev, req);

    completion_reset(&dev->req_completion);

    return req->status;

unlock_out:
    mtx_unlock(&dev->mtx);
    sdhci_finish_req(dev, req);
    return st;
}

static zx_status_t sdhci_perform_tuning(void* ctx) {
    zxlogf(TRACE, "sdhci: perform tuning\n");

    sdhci_device_t* dev = ctx;
    mtx_lock(&dev->mtx);

    // TODO no other commands should run during tuning

    sdmmc_req_t req = {
        .cmd_idx = MMC_SEND_TUNING_BLOCK,
        .cmd_flags = MMC_SEND_TUNING_BLOCK_FLAGS,
        .arg = 0,
        .blockcount = 0,
        .blocksize = (dev->regs->ctrl0 & SDHCI_HOSTCTRL_EXT_DATA_WIDTH) ? 128 : 64,
    };

    dev->regs->ctrl2 |= SDHCI_HOSTCTRL2_EXEC_TUNING;

    int count = 0;
    do {
        mtx_unlock(&dev->mtx);

        zx_status_t st = sdhci_request(dev, &req);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdhci: MMC_SEND_TUNING_BLOCK error, retcode = %d\n", req.status);
            return st;
        }

        mtx_lock(&dev->mtx);

    } while ((dev->regs->ctrl2 & SDHCI_HOSTCTRL2_EXEC_TUNING) && count++ < MAX_TUNING_COUNT);

    bool fail = (dev->regs->ctrl2 & SDHCI_HOSTCTRL2_EXEC_TUNING) ||
                !(dev->regs->ctrl2 & SDHCI_HOSTCTRL2_CLOCK_SELECT);

    mtx_unlock(&dev->mtx);

    zxlogf(TRACE, "sdhci: tuning fail %d\n", fail);

    if (fail) {
        return ZX_ERR_IO;
    } else {
        return ZX_OK;
    }
}

static zx_status_t sdhci_get_sdio_oob_irq(void* ctx, zx_handle_t *oob_irq_handle) {
    //Currently we do not support SDIO
    return ZX_ERR_NOT_SUPPORTED;
}

static sdmmc_protocol_ops_t sdmmc_proto = {
    .host_info = sdhci_host_info,
    .set_signal_voltage = sdhci_set_signal_voltage,
    .set_bus_width = sdhci_set_bus_width,
    .set_bus_freq = sdhci_set_bus_freq,
    .set_timing = sdhci_set_timing,
    .hw_reset = sdhci_hw_reset,
    .perform_tuning = sdhci_perform_tuning,
    .request = sdhci_request,
    .get_sdio_oob_irq = sdhci_get_sdio_oob_irq,
};

static void sdhci_unbind(void* ctx) {
    sdhci_device_t* dev = ctx;

    // stop irq thread
    zx_interrupt_destroy(dev->irq_handle);
    thrd_join(dev->irq_thread, NULL);

    device_remove(dev->zxdev);
}

static void sdhci_release(void* ctx) {
    sdhci_device_t* dev = ctx;
    zx_handle_close(dev->irq_handle);
    zx_handle_close(dev->bti_handle);
    zx_handle_close(dev->iobuf.vmo_handle);
    free(dev);
}

static zx_protocol_device_t sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = sdhci_unbind,
    .release = sdhci_release,
};

static zx_status_t sdhci_controller_init(sdhci_device_t* dev) {
    // Reset the controller.
    uint32_t ctrl1 = dev->regs->ctrl1;

    // Perform a software reset against both the DAT and CMD interface.
    ctrl1 |= SDHCI_SOFTWARE_RESET_ALL;

    // Disable both clocks.
    ctrl1 &= ~(SDHCI_INTERNAL_CLOCK_ENABLE | SDHCI_SD_CLOCK_ENABLE);

    // Write the register back to the device.
    dev->regs->ctrl1 = ctrl1;

    // Wait for reset to take place. The reset is comleted when all three
    // of the following flags are reset.
    const uint32_t target_mask = (SDHCI_SOFTWARE_RESET_ALL |
                                  SDHCI_SOFTWARE_RESET_CMD |
                                  SDHCI_SOFTWARE_RESET_DAT);
    zx_status_t status = ZX_OK;
    if ((status = sdhci_wait_for_reset(dev, target_mask, ZX_SEC(1))) != ZX_OK) {
        goto fail;
    }

    // allocate and setup DMA descriptor
    if (sdhci_supports_adma2_64bit(dev)) {
        status = io_buffer_init(&dev->iobuf, dev->bti_handle,
                                DMA_DESC_COUNT * sizeof(sdhci_adma64_desc_t),
                                IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            zxlogf(ERROR, "sdhci: error allocating DMA descriptors\n");
            goto fail;
        }
        dev->descs = io_buffer_virt(&dev->iobuf);
        dev->info.max_transfer_size = DMA_DESC_COUNT * PAGE_SIZE;

        // Select ADMA2
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_DMA_SELECT_ADMA2;
    } else {
        // no maximum if only PIO supported
        dev->info.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
    }

    // Configure the clock.
    ctrl1 = dev->regs->ctrl1;
    ctrl1 |= SDHCI_INTERNAL_CLOCK_ENABLE;

    // SDHCI Versions 1.00 and 2.00 handle the clock divider slightly
    // differently compared to SDHCI version 3.00. Since this driver doesn't
    // support SDHCI versions < 3.00, we ignore this incongruency for now.
    //
    // V3.00 supports a 10 bit divider where the SD clock frequency is defined
    // as F/(2*D) where F is the base clock frequency and D is the divider.
    const uint32_t divider = get_clock_divider(dev->base_clock, SD_FREQ_SETUP_HZ);
    const uint8_t divider_lo = divider & 0xff;
    const uint8_t divider_hi = (divider >> 8) & 0x3;
    ctrl1 |= ((divider_lo << 8) | (divider_hi << 6));

    // Set the command timeout.
    ctrl1 |= (0xe << 16);

    // Write back the clock frequency, command timeout and clock enable bits.
    dev->regs->ctrl1 = ctrl1;

    // Wait for the clock to stabilize.
    zx_time_t deadline = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_SEC(1);
    while (true) {
        if (((dev->regs->ctrl1) & SDHCI_INTERNAL_CLOCK_STABLE) != 0)
            break;

        if (zx_clock_get(ZX_CLOCK_MONOTONIC) > deadline) {
            zxlogf(ERROR, "sdhci: Clock did not stabilize in time\n");
            status = ZX_ERR_TIMED_OUT;
            goto fail;
        }
    }

    // Enable the SD clock.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));
    ctrl1 |= dev->regs->ctrl1;
    ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    dev->regs->ctrl1 = ctrl1;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));

    // Cut voltage to the card
    dev->regs->ctrl0 &= ~SDHCI_PWRCTRL_SD_BUS_POWER;

    // Set SD bus voltage to maximum supported by the host controller
    uint32_t ctrl0 = dev->regs->ctrl0 & ~SDHCI_PWRCTRL_SD_BUS_VOLTAGE_MASK;
    if (dev->info.caps & SDMMC_HOST_CAP_VOLTAGE_330) {
        ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
    } else {
        ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
    }
    dev->regs->ctrl0 = ctrl0;

    // Restore voltage to the card.
    dev->regs->ctrl0 |= SDHCI_PWRCTRL_SD_BUS_POWER;

    // Disable all interrupts
    dev->regs->irqen = 0;
    dev->regs->irq = 0xffffffff;

    return ZX_OK;
fail:
    return status;
}

static zx_status_t sdhci_bind(void* ctx, zx_device_t* parent) {
    sdhci_device_t* dev = calloc(1, sizeof(sdhci_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->req_completion = COMPLETION_INIT;

    zx_status_t status = ZX_OK;
    if (device_get_protocol(parent, ZX_PROTOCOL_SDHCI, (void*)&dev->sdhci)) {
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    // Map the Device Registers so that we can perform MMIO against the device.
    status = dev->sdhci.ops->get_mmio(dev->sdhci.ctx, &dev->regs);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdhci: error %d in get_mmio\n", status);
        goto fail;
    }

    status = dev->sdhci.ops->get_bti(dev->sdhci.ctx, 0, &dev->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdhci: error %d in get_bti\n", status);
        goto fail;
    }

    status = dev->sdhci.ops->get_interrupt(dev->sdhci.ctx, &dev->irq_handle);
    if (status < 0) {
        zxlogf(ERROR, "sdhci: error %d in get_interrupt\n", status);
        goto fail;
    }

    if (thrd_create_with_name(&dev->irq_thread, sdhci_irq_thread, dev, "sdhci_irq_thread") !=
        thrd_success) {
        zxlogf(ERROR, "sdhci: failed to create irq thread\n");
        goto fail;
    }

    // Ensure that we're SDv3.
    const uint16_t vrsn = (dev->regs->slotirqversion >> 16) & 0xff;
    if (vrsn != SDHCI_VERSION_3) {
        zxlogf(ERROR, "sdhci: SD version is %u, only version %u is supported\n",
                vrsn, SDHCI_VERSION_3);
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }
    zxlogf(TRACE, "sdhci: controller version %d\n", vrsn);

    dev->base_clock = ((dev->regs->caps0 >> 8) & 0xff) * 1000000; /* mhz */
    if (dev->base_clock == 0) {
        // try to get controller specific base clock
        dev->base_clock = dev->sdhci.ops->get_base_clock(dev->sdhci.ctx);
    }
    if (dev->base_clock == 0) {
        zxlogf(ERROR, "sdhci: base clock is 0!\n");
        status = ZX_ERR_INTERNAL;
        goto fail;
    }
    dev->quirks = dev->sdhci.ops->get_quirks(dev->sdhci.ctx);

    // Get controller capabilities
    uint32_t caps0 = dev->regs->caps0;
    if (caps0 & SDHCI_CORECFG_8_BIT_SUPPORT) {
        dev->info.caps |= SDMMC_HOST_CAP_BUS_WIDTH_8;
    }
    if (caps0 & SDHCI_CORECFG_ADMA2_SUPPORT) {
        dev->info.caps |= SDMMC_HOST_CAP_ADMA2;
    }
    if (caps0 & SDHCI_CORECFG_64BIT_SUPPORT) {
        dev->info.caps |= SDMMC_HOST_CAP_64BIT;
    }
    if (caps0 & SDHCI_CORECFG_3P3_VOLT_SUPPORT) {
        dev->info.caps |= SDMMC_HOST_CAP_VOLTAGE_330;
    }
    dev->info.caps |= SDMMC_HOST_CAP_AUTO_CMD12;

    // initialize the controller
    status = sdhci_controller_init(dev);
    if (status != ZX_OK) {
        goto fail;
    }

    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sdhci",
        .ctx = dev,
        .ops = &sdhci_device_proto,
        .proto_id = ZX_PROTOCOL_SDMMC,
        .proto_ops = &sdmmc_proto,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }
    return ZX_OK;
fail:
    if (dev) {
        sdhci_release(dev);
    }
    return status;
}

static zx_driver_ops_t sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sdhci_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(sdhci, sdhci_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SDHCI),
ZIRCON_DRIVER_END(sdhci)
// clang-format on
