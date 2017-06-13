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
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdhci.h>

// Magenta Includes
#include <mxio/watcher.h>
#include <magenta/threads.h>
#include <magenta/assert.h>
#include <sync/completion.h>

#define SD_FREQ_SETUP_HZ  400000

typedef struct sdhci_device {
    // Interrupts mapped here.
    mx_handle_t irq_handle;
    // Used to signal that a command has completed.
    completion_t irq_completion;

    // value of the irq register when the last irq fired (masked against the irqs
    // that were enabled at the time).
    uint32_t irq;

    // Memory mapped device registers.
    volatile sdhci_regs_t* regs;

    // Device heirarchy
    mx_device_t* mxdev;
    mx_device_t* parent;

    // Held when a command or action is in progress.
    mtx_t mtx;

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
    SDHCI_IRQ_BUFF_READ_READY |
    SDHCI_IRQ_BUFF_WRITE_READY
);

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
    printf("sdhci: entering irq thread\n");

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

        // Obtain the IRQs that were active when the interrupt fired.
        // Only stash the IRQs that were actually enabled.
        const uint32_t irq = (regs->irq & regs->irqen);

        // Stash these IRQs so that they can be processed by the caller.
        dev->irq = irq;

        // Acknowledge the IRQs that we stashed. IRQs are cleared by writing
        // 1s into the IRQs that fired.
        regs->irq = irq;

        // Mark this interrupt as completed.
        mx_interrupt_complete(irq_handle);

        // Signal that an IRQ happened.
        completion_signal(&dev->irq_completion);
    }

    printf("sdhci: irq_thread exit\n");
    return 0;
}

// Helper function that awaits an IRQ.
// Returns MX_OK if no error condition was detected, otherwise returns
// MX_ERR_IO.
static mx_status_t sdhci_await_irq(sdhci_device_t* dev) {
    mx_status_t st = completion_wait(&dev->irq_completion, MX_TIME_INFINITE);
    completion_reset(&dev->irq_completion);

    // Did completion wait return some kind of error?
    if (st != MX_OK)
        return st;

    // Was the IRQ triggered by an error interrupt?
    if (dev->irq & error_interrupts) {
        printf("sdhci: interrupt error = 0x%08x\n", (dev->irq & error_interrupts));
        return MX_ERR_IO;
    }

    return MX_OK;
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
    mx_status_t st;
    mtx_lock(&dev->mtx);

    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);

    volatile struct sdhci_regs* regs = dev->regs;
    const uint32_t arg = pdata->arg;
    const uint16_t blkcnt = pdata->blockcount;
    const uint16_t blksiz = pdata->blocksize;
    uint32_t cmd = pdata->cmd;

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
    if (cmd & SDMMC_RESP_DATA_PRESENT) {
        iotxn_physmap(txn);
        MX_DEBUG_ASSERT(txn->phys_count == 1);
        regs->arg2 = iotxn_phys(txn) + dev->dma_offset;

        iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, blkcnt * blksiz);

        if (cmd & SDMMC_CMD_MULTI_BLK)
            cmd |= SDMMC_CMD_AUTO12;
    }

    regs->blkcntsiz = (blksiz | (blkcnt << 16));

    regs->arg1 = arg;

    // Enable the appropriate interrupts.
    regs->irqmsk = error_interrupts | normal_interrupts;
    regs->irqen = error_interrupts | SDHCI_IRQ_CMD_CPLT;

    // Clear any pending interrupts before starting the transaction.
    regs->irq = regs->irqen;

    // And we're off to the races!
    regs->cmd = cmd;

    if ((st = sdhci_await_irq(dev)) != MX_OK) {
        iotxn_complete(txn, MX_ERR_IO, 0);
        goto exit;
    }

    // Read the response data.
    if ((cmd & SDMMC_RESP_LEN_136) && (dev->quirks & SDHCI_QUIRK_STRIP_RESPONSE_CRC)) {
        pdata->response[0] = (regs->resp3 << 8) | ((regs->resp2 >> 24) & 0xFF);
        pdata->response[1] = (regs->resp2 << 8) | ((regs->resp1 >> 24) & 0xFF);
        pdata->response[2] = (regs->resp1 << 8) | ((regs->resp0 >> 24) & 0xFF);
        pdata->response[3] = (regs->resp0 << 8);


    } else if (cmd & (SDMMC_RESP_LEN_48 | SDMMC_RESP_LEN_48B)) {
        pdata->response[0] = regs->resp0;
        pdata->response[1] = regs->resp1;
    }


    size_t bytes_copied = 0;
    if (cmd & SDMMC_RESP_DATA_PRESENT) {
        // Select the interrupt that we want to wait on based on whether we're
        // reading or writing.
        if (cmd & SDMMC_CMD_READ) {
            regs->irqen = error_interrupts | SDHCI_IRQ_BUFF_READ_READY;
        } else {
            regs->irqen = error_interrupts | SDHCI_IRQ_BUFF_WRITE_READY;
        }

        // Sequentially read or write each block.
        // BCM28xx quirk: The BCM28xx appears to use its internal DMA engine to
        // perform transfers against the SD card. Normally we would use SDMA or
        // ADMA (if the part supported it). Since this part doesn't appear to
        // support either, we just use PIO.
        // TODO(yky): add DMA mode if it works on intel and add quirk/detection
        // to do PIO
        for (size_t blkid = 0; blkid < blkcnt; blkid++) {
            mx_status_t st;
            if ((st = sdhci_await_irq(dev)) != MX_OK) {
                iotxn_complete(txn, st, bytes_copied);
                goto exit;
            }

            uint32_t wrd;
            for (size_t byteid = 0; byteid < blksiz; byteid += 4) {
                const size_t offset = blkid * blksiz + byteid;
                if (cmd & SDMMC_CMD_READ) {
                    wrd = regs->data;
                    iotxn_copyto(txn, &wrd, sizeof(wrd), offset);
                } else {
                    iotxn_copyfrom(txn, &wrd, sizeof(wrd), offset);
                    regs->data = wrd;
                }
                bytes_copied += sizeof(wrd);
            }
        }

        if ((regs->state & SDHCI_STATE_DAT_INHIBIT) == 0) {
            regs->irq = 0xffff0002;
        }
    }

    iotxn_complete(txn, MX_OK, bytes_copied);

exit:
    mtx_unlock(&dev->mtx);
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

static mx_status_t sdhci_set_bus_width(sdhci_device_t* dev, const uint32_t new_bus_width) {
    switch (new_bus_width) {
        case 1:
            dev->regs->ctrl0 &= ~SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
            break;
        case 4:
            dev->regs->ctrl0 |= SDHCI_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
            break;
        default:
            return MX_ERR_INVALID_ARGS;
    }

    return MX_OK;
}

static mx_status_t sdhci_set_voltage(sdhci_device_t* dev, uint32_t new_voltage) {

    switch (new_voltage) {
        case SDMMC_VOLTAGE_33:
        case SDMMC_VOLTAGE_30:
        case SDMMC_VOLTAGE_18:
            break;
        default:
            return MX_ERR_INVALID_ARGS;
    }

    volatile struct sdhci_regs* regs = dev->regs;

    // Disable the SD clock before messing with the voltage.
    regs->ctrl1 &= ~SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Wait for the DAT lines to settle
    const mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);
    while (true) {
        printf("Waiting for dat lines to go to 0\n");
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            return MX_ERR_TIMED_OUT;
        }

        uint8_t dat_lines = ((regs->state) >> 20) & 0xf;
        if (dat_lines == 0) break;

        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }

    // Cut voltage to the card
    regs->ctrl0 &= ~SDHCI_PWRCTRL_SD_BUS_POWER;

    regs->ctrl0 |= new_voltage;

    // Restore voltage to the card.
    regs->ctrl0 |= SDHCI_PWRCTRL_SD_BUS_POWER;

    // Make sure our changes are acknolwedged.
    const uint32_t expected_mask = (SDHCI_PWRCTRL_SD_BUS_POWER) | (new_voltage);
    if ((regs->ctrl0 & expected_mask) != expected_mask) return MX_ERR_INTERNAL;

    // Turn the clock back on
    regs->ctrl1 |= SDHCI_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return MX_OK;
}

static mx_status_t sdhci_ioctl(void* ctx, uint32_t op,
                          const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len, size_t* out_actual) {
    sdhci_device_t* dev = ctx;
    uint32_t* arg;
    arg = (uint32_t*)in_buf;
    if (in_len < sizeof(*arg))
        return MX_ERR_INVALID_ARGS;

    switch (op) {
    case IOCTL_SDMMC_SET_VOLTAGE:
        return sdhci_set_voltage(dev, *arg);
    case IOCTL_SDMMC_SET_BUS_WIDTH:
        if ((*arg != 4) && (*arg != 1))
            return MX_ERR_INVALID_ARGS;
        return sdhci_set_bus_width(dev, *arg);
    case IOCTL_SDMMC_SET_BUS_FREQ:
        printf("sdhci: ioctl set bus frequency to %u\n", *arg);
        return sdhci_set_bus_frequency(dev, *arg);
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

    // Wait for th reset to take place. The reset is comleted when all three
    // of the following flags are reset.
    const uint32_t target_mask = (SDHCI_SOFTWARE_RESET_ALL |
                                  SDHCI_SOFTWARE_RESET_CMD |
                                  SDHCI_SOFTWARE_RESET_DAT);
    mx_time_t deadline =
        mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);

    mx_status_t status = MX_OK;
    while (true) {
        if (((dev->regs->ctrl1) & target_mask) == 0)
            break;

        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            printf("sdhci: timed out while waiting for reset\n");
            status = MX_ERR_TIMED_OUT;
            goto fail;
        }
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
    deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);
    while (true) {
        if (((dev->regs->ctrl1) & SDHCI_INTERNAL_CLOCK_STABLE) != 0)
            break;

        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            printf("sdhci: Clock did not stabilize in time\n");
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

    // Disable all interrupts
    dev->regs->irqen = 0;
    dev->regs->irq = 0xffffffff;

    return MX_OK;
fail:
    return status;
}

static mx_status_t sdhci_bind(void* ctx, mx_device_t* parent, void** cookie) {
    sdhci_protocol_t sdhci;
    if (device_get_protocol(parent, MX_PROTOCOL_SDHCI, (void*)&sdhci)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    sdhci_device_t* dev = calloc(1, sizeof(sdhci_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }

    // Map the Device Registers so that we can perform MMIO against the device.
    mx_status_t status = sdhci.ops->get_mmio(sdhci.ctx, &dev->regs);
    if (status != MX_OK) {
        printf("sdhci: error %d in get_mmio\n", status);
        goto fail;
    }

    dev->irq_handle = sdhci.ops->get_interrupt(sdhci.ctx);
    if (dev->irq_handle < 0) {
        printf("sdhci: error %d in get_interrupt\n", status);
        status = dev->irq_handle;
        goto fail;
    }

    thrd_t irq_thread;
    if (thrd_create_with_name(&irq_thread, sdhci_irq_thread, dev, "sdhci_irq_thread") != thrd_success) {
        printf("sdhci: failed to create irq thread\n");
        goto fail;
    }
    thrd_detach(irq_thread);

    dev->irq_completion = COMPLETION_INIT;
    dev->parent = parent;

    // Ensure that we're SDv3 or above.
    const uint16_t vrsn = (dev->regs->slotirqversion >> 16) & 0xff;
    if (vrsn < SDHCI_VERSION_3) {
        printf("sdhci: SD version is %u, only version %u and above are "
                "supported\n", vrsn, SDHCI_VERSION_3);
        status = MX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    dev->base_clock = sdhci.ops->get_base_clock(sdhci.ctx);
    if (dev->base_clock == 0) {
        printf("sdhci: base clock is 0!\n");
        status = MX_ERR_INTERNAL;
        goto fail;
    }
    dev->dma_offset = sdhci.ops->get_dma_offset(sdhci.ctx);
    dev->quirks = sdhci.ops->get_quirks(sdhci.ctx);

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
