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
#include <ddk/iotxn.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdhci.h>
#include <hw/sdmmc.h>

// Magenta Includes
#include <mxio/watcher.h>
#include <magenta/threads.h>
#include <magenta/assert.h>
#include <sync/completion.h>
#include <pretty/hexdump.h>

#define SD_FREQ_SETUP_HZ  400000

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define HI32(val)   (((val) >> 32) & 0xffffffff)
#define LO32(val)   ((val) & 0xffffffff)

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
    uint32_t reserved;
} __PACKED sdhci_adma64_desc_t;

static_assert(sizeof(sdhci_adma64_desc_t) == 16, "unexpected ADMA2 descriptor size");

#define ADMA2_DESC_MAX_LENGTH   0x10000 // 64k
#define DMA_DESC_COUNT          8192    // for 32M max transfer size for fully discontiguous

typedef struct sdhci_device {
    // Interrupts mapped here.
    mx_handle_t irq_handle;
    // Used to signal that a command has completed.
    completion_t irq_completion;

    // Memory mapped device registers.
    volatile sdhci_regs_t* regs;

    // Device heirarchy
    mx_device_t* mxdev;
    mx_device_t* parent;

    // Protocol ops
    sdhci_protocol_t sdhci;

    // DMA descriptors
    io_buffer_t iobuf;
    sdhci_adma64_desc_t* descs;

    // Held when a command or action is in progress.
    mtx_t mtx;

    // Current iotxn in flight
    iotxn_t* pending;
    // Completed iotxn
    iotxn_t* completed;
    // Used to signal that the pending iotxn is completed
    completion_t pending_completion;

    // controller specific quirks
    uint64_t quirks;

    // Cached base clock rate that the pi is running at.
    uint32_t base_clock;
    // Offset to DMA address
    // XXX temporary (see ddk/protocol/sdhci.h)
    mx_paddr_t dma_offset;
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

static bool sdhci_supports_adma2_64bit(sdhci_device_t* dev) {
    return (dev->regs->caps0 & SDHCI_CORECFG_ADMA2_SUPPORT) &&
           (dev->regs->caps0 & SDHCI_CORECFG_64BIT_SUPPORT) &&
           !(dev->quirks & SDHCI_QUIRK_NO_DMA);
}

static mx_status_t sdhci_wait_for_reset(sdhci_device_t* dev, const uint32_t mask, mx_time_t timeout) {
    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + timeout;
    while (true) {
        if (((dev->regs->ctrl1) & mask) == 0) {
            break;
        }
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            printf("sdhci: timed out while waiting for reset\n");
            return MX_ERR_TIMED_OUT;
        }
    }
    return MX_OK;
}

static void sdhci_complete_pending_locked(sdhci_device_t* dev, mx_status_t status, uint64_t actual) {
    // Disable irqs when no pending iotxn
    dev->regs->irqen = 0;

    dev->completed = dev->pending;
    dev->completed->status = status;
    dev->completed->actual = actual;
    dev->pending = NULL;

    completion_signal(&dev->pending_completion);
}

static void sdhci_cmd_stage_complete_locked(sdhci_device_t* dev) {
    if (!dev->pending) {
        xprintf("sdhci: spurious CMD_CPLT interrupt!\n");
        return;
    }

    iotxn_t* txn = dev->pending;
    volatile struct sdhci_regs* regs = dev->regs;
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    uint32_t cmd = pdata->cmd;

    // Read the response data.
    if (cmd & SDMMC_RESP_LEN_136) {
        if (dev->quirks & SDHCI_QUIRK_STRIP_RESPONSE_CRC) {
            pdata->response[0] = (regs->resp3 << 8) | ((regs->resp2 >> 24) & 0xFF);
            pdata->response[1] = (regs->resp2 << 8) | ((regs->resp1 >> 24) & 0xFF);
            pdata->response[2] = (regs->resp1 << 8) | ((regs->resp0 >> 24) & 0xFF);
            pdata->response[3] = (regs->resp0 << 8);
        } else {
            pdata->response[0] = regs->resp0;
            pdata->response[1] = regs->resp1;
            pdata->response[2] = regs->resp2;
            pdata->response[3] = regs->resp3;
        }
    } else if (cmd & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
        pdata->response[0] = regs->resp0;
        pdata->response[1] = regs->resp1;
    }

    // If this command has a data phase and we're not using DMA, transfer the data
    bool has_data = cmd & SDMMC_RESP_DATA_PRESENT;
    bool use_dma = sdhci_supports_adma2_64bit(dev);
    if (has_data) {
        if (use_dma) {
            // Wait for transfer complete interrupt
            regs->irqen = error_interrupts | SDHCI_IRQ_XFER_CPLT;
        } else {
            // Select the interrupt that we want to wait on based on whether we're
            // reading or writing.
            if (cmd & SDMMC_CMD_READ) {
                regs->irqen = error_interrupts | SDHCI_IRQ_BUFF_READ_READY;
            } else {
                regs->irqen = error_interrupts | SDHCI_IRQ_BUFF_WRITE_READY;
            }
        }
    } else {
        sdhci_complete_pending_locked(dev, MX_OK, 0);
    }
}

static void sdhci_data_stage_read_ready_locked(sdhci_device_t* dev) {
    if (!dev->pending) {
        xprintf("sdhci: spurious BUFF_READ_READY interrupt!\n");
        return;
    }

    iotxn_t* txn = dev->pending;
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);

    // Sequentially read each block.
    for (size_t byteid = 0; byteid < pdata->blocksize; byteid += 4) {
        uint32_t wrd;
        const size_t offset = pdata->blockid * pdata->blocksize + byteid;
        wrd = dev->regs->data;
        iotxn_copyto(txn, &wrd, sizeof(wrd), offset);
        txn->actual += sizeof(wrd);
    }
    pdata->blockid += 1;
    if (pdata->blockid == pdata->blockcount) {
        sdhci_complete_pending_locked(dev, MX_OK, txn->actual);
    }
}

static void sdhci_data_stage_write_ready_locked(sdhci_device_t* dev) {
    if (!dev->pending) {
        xprintf("sdhci: spurious BUFF_WRITE_READY interrupt!\n");
        return;
    }

    iotxn_t* txn = dev->pending;
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);

    // Sequentially write each block.
    for (size_t byteid = 0; byteid < pdata->blocksize; byteid += 4) {
        uint32_t wrd;
        const size_t offset = pdata->blockid * pdata->blocksize + byteid;
        iotxn_copyfrom(txn, &wrd, sizeof(wrd), offset);
        dev->regs->data = wrd;
        txn->actual += sizeof(wrd);
    }
    pdata->blockid += 1;
    if (pdata->blockid == pdata->blockcount) {
        sdhci_complete_pending_locked(dev, MX_OK, txn->actual);
    }
}

static void sdhci_transfer_complete_locked(sdhci_device_t* dev) {
    if (!dev->pending) {
        xprintf("sdhci: spurious XFER_CPLT interrupt!\n");
        return;
    }
    sdhci_complete_pending_locked(dev, MX_OK, dev->pending->length);
}

static void sdhci_error_recovery_locked(sdhci_device_t* dev) {
    // Reset internal state machines
    dev->regs->ctrl1 |= SDHCI_SOFTWARE_RESET_CMD;
    sdhci_wait_for_reset(dev, SDHCI_SOFTWARE_RESET_CMD, MX_SEC(1));
    dev->regs->ctrl1 |= SDHCI_SOFTWARE_RESET_DAT;
    sdhci_wait_for_reset(dev, SDHCI_SOFTWARE_RESET_DAT, MX_SEC(1));

    // TODO data stage abort

    // Complete any pending txn with error status
    if (dev->pending != NULL) {
        sdhci_complete_pending_locked(dev, MX_ERR_IO, 0);
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
    mx_status_t wait_res;
    sdhci_device_t* dev = (sdhci_device_t*)arg;
    volatile struct sdhci_regs* regs = dev->regs;
    mx_handle_t irq_handle = dev->irq_handle;

    while (true) {
        wait_res = mx_interrupt_wait(irq_handle);
        if (wait_res != MX_OK) {
            printf("sdhci: interrupt wait failed with retcode = %d\n", wait_res);
            break;
        }

        const uint32_t irq = regs->irq;
        xprintf("got irq 0x%08x 0x%08x en 0x%08x\n", regs->irq, irq, regs->irqen);

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
            sdhci_error_recovery_locked(dev);
        }
        mtx_unlock(&dev->mtx);

        // Mark this interrupt as completed.
        mx_interrupt_complete(irq_handle);
    }
    return 0;
}

static mx_status_t sdhci_start_txn_locked(sdhci_device_t* dev, iotxn_t* txn) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);

    volatile struct sdhci_regs* regs = dev->regs;
    const uint32_t arg = pdata->arg;
    const uint16_t blkcnt = pdata->blockcount;
    const uint16_t blksiz = pdata->blocksize;
    uint32_t cmd = pdata->cmd;

    mx_status_t st = MX_OK;

#if 1
    xprintf("sdhci: start_txn cmd=0x%08x (data %d) blkcnt %u blksiz %u length %" PRIu64 "\n",
            cmd, !!(cmd & SDMMC_RESP_DATA_PRESENT), blkcnt, blksiz, txn->length);
#endif

    pdata->blockid = 0;
    txn->actual = 0;

    // Every command requires that the Command Inhibit is unset.
    uint32_t inhibit_mask = SDHCI_STATE_CMD_INHIBIT;

    // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
    // it's an abort command which can be issued with the data lines active.
    if ((cmd & SDMMC_RESP_LEN_48B) && ((cmd & SDMMC_CMD_TYPE_ABORT) == 0)) {
        inhibit_mask |= SDHCI_STATE_DAT_INHIBIT;
    }

    // Wait for the inhibit masks from above to become 0 before issuing the
    // command.
    while (regs->state & inhibit_mask)
        mx_nanosleep(mx_deadline_after(MX_MSEC(1)));

    // This command has a data phase?
    bool has_data = cmd & SDMMC_RESP_DATA_PRESENT;
    bool use_dma = sdhci_supports_adma2_64bit(dev);
    if (has_data) {
        st = iotxn_physmap(txn);
        if (st != MX_OK) {
            goto err;
        }
        iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, blkcnt * blksiz);

        if (use_dma) {
            iotxn_phys_iter_t iter;
            iotxn_phys_iter_init(&iter, txn, ADMA2_DESC_MAX_LENGTH);

            int count = 0;
            size_t length;
            mx_paddr_t paddr;
            sdhci_adma64_desc_t* desc = dev->descs;
            for (;;) {
                length = iotxn_phys_iter_next(&iter, &paddr);
                if (length == 0) {
                    if (desc != dev->descs) {
                        desc -= 1;
                        desc->end = 1; // set end bit on the last descriptor
                        break;
                    } else {
                        xprintf("sdhci: empty descriptor list!\n");
                        st = MX_ERR_NOT_SUPPORTED;
                        goto err;
                    }
                } else if (length > ADMA2_DESC_MAX_LENGTH) {
                    xprintf("sdhci: chunk size > %zu is unsupported\n", length);
                    st = MX_ERR_NOT_SUPPORTED;
                    goto err;
                } else if ((++count) > DMA_DESC_COUNT) {
                    xprintf("sdhci: txn with more than %zd chunks is unsupported\n", length);
                    st = MX_ERR_NOT_SUPPORTED;
                    goto err;
                }
                desc->length = length & 0xffff; // 0 = 0x10000 bytes
                desc->address = paddr;
                desc->attr = 0;
                desc->valid = 1;
                desc->act2 = 1; // transfer data
                desc += 1;
            }

#if TRACE
            desc = dev->descs;
            do {
                xprintf("desc: addr=0x%" PRIx64 " length=0x%04x attr=0x%04x\n", desc->address, desc->length, desc->attr);
            } while (!(desc++)->end);
#endif

            mx_paddr_t desc_phys = io_buffer_phys(&dev->iobuf);
            dev->regs->admaaddr0 = LO32(desc_phys);
            dev->regs->admaaddr1 = HI32(desc_phys);

            cmd |= SDHCI_XFERMODE_DMA_ENABLE;

        } else {
            MX_DEBUG_ASSERT(txn->phys_count == 1);
            regs->arg2 = iotxn_phys(txn) + dev->dma_offset;
        }

        if (cmd & SDMMC_CMD_MULTI_BLK) {
            cmd |= SDMMC_CMD_AUTO12;
        }
    }

    regs->blkcntsiz = (blksiz | (blkcnt << 16));

    regs->arg1 = arg;

    // Unmask and enable command complete interrupt
    regs->irqmsk = error_interrupts | normal_interrupts;
    regs->irqen = error_interrupts | SDHCI_IRQ_CMD_CPLT;

    // Clear any pending interrupts before starting the transaction.
    regs->irq = regs->irqen;

    // And we're off to the races!
    regs->cmd = cmd;
    return MX_OK;
err:
    return st;
}

static void sdhci_iotxn_queue(void* ctx, iotxn_t* txn) {
    // Ensure that the offset is some multiple of the block size, we don't allow
    // writes that are partway into a block.
    if (txn->offset % SDHC_BLOCK_SIZE) {
        printf("sdhci: iotxn offset not aligned to block boundary, "
               "offset =%" PRIu64", block size = %d\n", txn->offset, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }

    // Ensure that the length of the write is some multiple of the block size.
    if (txn->length % SDHC_BLOCK_SIZE) {
        printf("sdhci: iotxn length not aligned to block boundary, "
               "offset =%" PRIu64", block size = %d\n", txn->length, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }

    sdhci_device_t* dev = ctx;

    // One at a time for now
    mtx_lock(&dev->mtx);
    if (dev->pending != NULL) {
        mtx_unlock(&dev->mtx);
        printf("sdhci: only one outstanding iotxn is allowed\n");
        iotxn_complete(txn, MX_ERR_NO_RESOURCES, 0);
        return;
    }

    // Start the txn
    dev->pending = txn;
    mx_status_t st;
    if ((st = sdhci_start_txn_locked(dev, txn)) != MX_OK) {
        dev->pending = NULL;
        mtx_unlock(&dev->mtx);
        iotxn_complete(txn, MX_ERR_NO_RESOURCES, 0);
        return;
    }

    mtx_unlock(&dev->mtx);

    // Wait for completion
    do {
        completion_wait(&dev->pending_completion, MX_TIME_INFINITE);
        completion_reset(&dev->pending_completion);

        mtx_lock(&dev->mtx);

        if (!dev->completed || (dev->completed != txn)) {
            printf("sdhci: spurious completion\n");
            mtx_unlock(&dev->mtx);
            continue;

        } else {
            dev->completed = NULL;
            mtx_unlock(&dev->mtx);
            break;
        }
    } while (true);

    iotxn_complete(txn, txn->status, txn->actual);
}

static mx_status_t sdhci_set_bus_frequency(sdhci_device_t* dev, uint32_t target_freq) {
    const uint32_t divider = get_clock_divider(dev->base_clock, target_freq);
    const uint8_t divider_lo = divider & 0xff;
    const uint8_t divider_hi = (divider >> 8) & 0x3;

    volatile struct sdhci_regs* regs = dev->regs;

    uint32_t iterations = 0;
    while (regs->state & (SDHCI_STATE_CMD_INHIBIT | SDHCI_STATE_DAT_INHIBIT)) {
        if (++iterations > 1000)
            return MX_ERR_TIMED_OUT;

        mx_nanosleep(mx_deadline_after(MX_MSEC(1)));
    }

    // Turn off the SD clock before messing with the clock rate.
    regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Write the new divider into the control register.
    uint32_t ctrl1 = regs->ctrl1;
    ctrl1 &= ~0xffe0;
    ctrl1 |= ((divider_lo << 8) | (divider_hi << 6));
    regs->ctrl1 = ctrl1;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Turn the SD clock back on.
    regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return MX_OK;
}

static mx_status_t sdhci_set_timing(sdhci_device_t* dev, uint32_t timing) {
    // Toggle high-speed
    if (timing != SDMMC_TIMING_LEGACY) {
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
    } else {
        dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_HIGHSPEED_ENABLE;
    }

    // Disable SD clock before changing UHS timing
    dev->regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    uint32_t ctrl2 = dev->regs->ctrl2 & ~SDHCI_HOSTCTRL2_UHS_MODE_SELECT_MASK;
    if (timing == SDMMC_TIMING_HS200) {
        ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_SDR104;
    } else if (timing == SDMMC_TIMING_HS400) {
        ctrl2 |= SDHCI_HOSTCTRL2_UHS_MODE_SELECT_HS400;
    }
    dev->regs->ctrl2 = ctrl2;

    // Turn the SD clock back on.
    dev->regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return MX_OK;
}

static void sdhci_hw_reset(sdhci_device_t* dev) {
    if (dev->sdhci.ops->hw_reset) {
        dev->sdhci.ops->hw_reset(dev->sdhci.ctx);
    }
}

static mx_status_t sdhci_set_bus_width(sdhci_device_t* dev, const uint32_t new_bus_width) {
    if ((new_bus_width == SDMMC_BUS_WIDTH_8) &&
        !(dev->regs->caps0 & SDHCI_CORECFG_8_BIT_SUPPORT)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    switch (new_bus_width) {
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
        return MX_ERR_INVALID_ARGS;
    }

    return MX_OK;
}

static mx_status_t sdhci_set_signal_voltage(sdhci_device_t* dev, uint32_t new_voltage) {

    switch (new_voltage) {
        case SDMMC_SIGNAL_VOLTAGE_330:
        case SDMMC_SIGNAL_VOLTAGE_180:
            break;
        default:
            return MX_ERR_INVALID_ARGS;
    }

    volatile struct sdhci_regs* regs = dev->regs;

    // Disable the SD clock before messing with the voltage.
    regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    if (new_voltage == SDMMC_SIGNAL_VOLTAGE_180) {
        regs->ctrl2 |= SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
        // 1.8V regulator out should be stable within 5ms
        mx_nanosleep(mx_deadline_after(MX_MSEC(5)));
#if TRACE
        if (!(regs->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA)) {
            xprintf("sdhci: 1.8V regulator output did not become stable\n");
        }
#endif
    } else {
        regs->ctrl2 &= ~SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA;
        // 3.3V regulator out should be stable within 5ms
        mx_nanosleep(mx_deadline_after(MX_MSEC(5)));
#if TRACE
        if (regs->ctrl2 & SDHCI_HOSTCTRL2_1P8V_SIGNALLING_ENA) {
            xprintf("sdhci: 3.3V regulator output did not become stable\n");
        }
#endif
    }

    // Make sure our changes are acknolwedged.
    uint32_t expected_mask = SDHCI_PWRCTRL_SD_BUS_POWER;
    if (new_voltage == SDMMC_SIGNAL_VOLTAGE_180) {
        expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
    } else {
        expected_mask |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
    }
    if ((regs->ctrl0 & expected_mask) != expected_mask) {
        xprintf("sdhci: after voltage switch ctrl0=0x%08x, expected=0x%08x\n", regs->ctrl0, expected_mask);
        return MX_ERR_INTERNAL;
    }

    // Turn the clock back on
    regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return MX_OK;
}

static mx_status_t sdhci_ioctl(void* ctx, uint32_t op,
                          const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len, size_t* out_actual) {
    sdhci_device_t* dev = ctx;
    uint32_t* arg = (uint32_t*)in_buf;

    switch (op) {
    case IOCTL_SDMMC_SET_SIGNAL_VOLTAGE:
        if (in_len < sizeof(*arg)) {
            return MX_ERR_INVALID_ARGS;
        } else {
            return sdhci_set_signal_voltage(dev, *arg);
        }
    case IOCTL_SDMMC_SET_BUS_WIDTH:
        if (in_len < sizeof(*arg)) {
            return MX_ERR_INVALID_ARGS;
        } else {
            return sdhci_set_bus_width(dev, *arg);
        }
    case IOCTL_SDMMC_SET_BUS_FREQ:
        if (in_len < sizeof(*arg)) {
            return MX_ERR_INVALID_ARGS;
        } else {
            return sdhci_set_bus_frequency(dev, *arg);
        }
    case IOCTL_SDMMC_SET_TIMING:
        if (in_len < sizeof(*arg)) {
            return MX_ERR_INVALID_ARGS;
        } else {
            return sdhci_set_timing(dev, *arg);
        }
    case IOCTL_SDMMC_HW_RESET:
        sdhci_hw_reset(dev);
        return MX_OK;
    }

    return MX_ERR_NOT_SUPPORTED;
}

static void sdhci_unbind(void* ctx) {
    sdhci_device_t* dev = ctx;
    device_remove(dev->mxdev);
}

static void sdhci_release(void* ctx) {
    sdhci_device_t* dev = ctx;
    free(dev);
}

static mx_protocol_device_t sdhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = sdhci_iotxn_queue,
    .ioctl = sdhci_ioctl,
    .unbind = sdhci_unbind,
    .release = sdhci_release,
};

static mx_status_t sdhci_controller_init(sdhci_device_t* dev) {
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
    mx_status_t status = MX_OK;
    if ((status = sdhci_wait_for_reset(dev, target_mask, MX_SEC(1))) != MX_OK) {
        goto fail;
    }

    // allocate and setup DMA descriptor
    if (sdhci_supports_adma2_64bit(dev)) {
        status = io_buffer_init(&dev->iobuf, DMA_DESC_COUNT * sizeof(sdhci_adma64_desc_t), IO_BUFFER_RW);
        if (status != MX_OK) {
            xprintf("sdhci: error allocating DMA descriptors\n");
            goto fail;
        }
        dev->descs = io_buffer_virt(&dev->iobuf);

        // Select ADMA2
        dev->regs->ctrl0 |= SDHCI_HOSTCTRL_DMA_SELECT_ADMA2;
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
    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);
    while (true) {
        if (((dev->regs->ctrl1) & SDHCI_INTERNAL_CLOCK_STABLE) != 0)
            break;

        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            xprintf("sdhci: Clock did not stabilize in time\n");
            status = MX_ERR_TIMED_OUT;
            goto fail;
        }
    }

    // Enable the SD clock.
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));
    ctrl1 |= dev->regs->ctrl1;
    ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    dev->regs->ctrl1 = ctrl1;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Cut voltage to the card
    dev->regs->ctrl0 &= ~SDHCI_PWRCTRL_SD_BUS_POWER;

    // Set SD bus voltage to maximum supported by the host controller
    const uint32_t caps = dev->regs->caps0;
    uint32_t ctrl0 = dev->regs->ctrl0 & ~SDHCI_PWRCTRL_SD_BUS_VOLTAGE_MASK;
    if (caps & SDHCI_CORECFG_3P3_VOLT_SUPPORT) {
        ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P3V;
    } else if (caps & SDHCI_CORECFG_3P0_VOLT_SUPPORT) {
        ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_3P0V;
    } else {
        ctrl0 |= SDHCI_PWRCTRL_SD_BUS_VOLTAGE_1P8V;
    }
    dev->regs->ctrl0 = ctrl0;

    // Restore voltage to the card.
    dev->regs->ctrl0 |= SDHCI_PWRCTRL_SD_BUS_POWER;

    // Disable all interrupts
    dev->regs->irqen = 0;
    dev->regs->irq = 0xffffffff;

    return MX_OK;
fail:
    return status;
}

static mx_status_t sdhci_bind(void* ctx, mx_device_t* parent, void** cookie) {
    sdhci_device_t* dev = calloc(1, sizeof(sdhci_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }

    mx_status_t status = MX_OK;
    if (device_get_protocol(parent, MX_PROTOCOL_SDHCI, (void*)&dev->sdhci)) {
        status = MX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    // Map the Device Registers so that we can perform MMIO against the device.
    status = dev->sdhci.ops->get_mmio(dev->sdhci.ctx, &dev->regs);
    if (status != MX_OK) {
        xprintf("sdhci: error %d in get_mmio\n", status);
        goto fail;
    }

    dev->irq_handle = dev->sdhci.ops->get_interrupt(dev->sdhci.ctx);
    if (dev->irq_handle < 0) {
        xprintf("sdhci: error %d in get_interrupt\n", status);
        status = dev->irq_handle;
        goto fail;
    }

    thrd_t irq_thread;
    if (thrd_create_with_name(&irq_thread, sdhci_irq_thread, dev, "sdhci_irq_thread") != thrd_success) {
        xprintf("sdhci: failed to create irq thread\n");
        goto fail;
    }
    thrd_detach(irq_thread);

    dev->irq_completion = COMPLETION_INIT;
    dev->pending_completion = COMPLETION_INIT;
    dev->parent = parent;

    // Ensure that we're SDv3 or above.
    const uint16_t vrsn = (dev->regs->slotirqversion >> 16) & 0xff;
    if (vrsn < SDHCI_VERSION_3) {
        xprintf("sdhci: SD version is %u, only version %u and above are "
                "supported\n", vrsn, SDHCI_VERSION_3);
        status = MX_ERR_NOT_SUPPORTED;
        goto fail;
    }
    xprintf("sdhci: controller version %d\n", vrsn);

    dev->base_clock = ((dev->regs->caps0 >> 8) & 0xff) * 1000000; /* mhz */
    if (dev->base_clock == 0) {
        // try to get controller specific base clock
        dev->base_clock = dev->sdhci.ops->get_base_clock(dev->sdhci.ctx);
    }
    if (dev->base_clock == 0) {
        xprintf("sdhci: base clock is 0!\n");
        status = MX_ERR_INTERNAL;
        goto fail;
    }
    dev->dma_offset = dev->sdhci.ops->get_dma_offset(dev->sdhci.ctx);
    dev->quirks = dev->sdhci.ops->get_quirks(dev->sdhci.ctx);

    // initialize the controller
    status = sdhci_controller_init(dev);
    if (status != MX_OK) {
        goto fail;
    }

    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sdhci",
        .ctx = dev,
        .ops = &sdhci_device_proto,
        .proto_id = MX_PROTOCOL_SDMMC,
    };

    status = device_add(parent, &args, &dev->mxdev);
    if (status != MX_OK) {
        goto fail;
    }
    return MX_OK;
fail:
    if (dev) {
        if (dev->irq_handle != MX_HANDLE_INVALID) {
            mx_handle_close(dev->irq_handle);
        }
        if (dev->iobuf.vmo_handle != MX_HANDLE_INVALID) {
            mx_handle_close(dev->iobuf.vmo_handle);
        }
        free(dev);
    }
    return status;
}

static mx_driver_ops_t sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sdhci_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
MAGENTA_DRIVER_BEGIN(sdhci, sdhci_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_SDHCI),
MAGENTA_DRIVER_END(sdhci)
// clang-format on
