// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Notes and limitations:
// 1. This driver _almost_ implements the standard SDHCI spec but doesn't quite
//    conform entirely due to idiosyncrasies in the Pi3's controller. For
//    example, this driver relies on the VC-mailbox device to get the base clock
//    rate for the device and to power the device on. Additionally, the Pi3's
//    controller does not appear to support any type of DMA natively and relies
//    on the BCM28xx's DMA controller for DMA. For this reason, this driver uses
//    PIO to communicate with the device. A more complete (and generic) driver
//    might attempt [S/A]DMA and fall back on PIO in case of failure.
//    Additionally, the Pi's controller doesn't appear to populate the SDHCI
//    capabilities registers to expose what capabilities the EMMC controller
//    provides.
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
#include <ddk/protocol/bcm-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/sdmmc.h>

// Magenta Includes
#include <mxio/watcher.h>
#include <magenta/threads.h>
#include <magenta/assert.h>
#include <sync/completion.h>

// BCM28xx Specific Includes
#include <bcm/bcm28xx.h>
#include <bcm/ioctl.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define PAGE_MASK_4K (0xFFF)
#define SDMMC_PAGE_START (EMMC_BASE & (~PAGE_MASK_4K))
#define SDMMC_PAGE_SIZE (0x1000)

#define SD_FREQ_SETUP_HZ  400000

struct emmc_regs {
    uint32_t arg2;          // 00h
    uint32_t blkcntsiz;     // 04h
    uint32_t arg1;          // 08h
    uint32_t cmd;           // 0Ch
    uint32_t resp0;         // 10h
    uint32_t resp1;         // 14h
    uint32_t resp2;         // 18h
    uint32_t resp3;         // 1Ch
    uint32_t data;          // 20h
    uint32_t state;         // 24h
        #define EMMC_STATE_CMD_INHIBIT           (1 << 0)
        #define EMMC_STATE_DAT_INHIBIT           (1 << 1)
        #define EMMC_STATE_DAT_LINE_ACTIVE       (1 << 2)
        #define EMMC_STATE_RETUNING_REQUEST      (1 << 3)
        #define EMMC_STATE_WRITE_TRANSFER_ACTIVE (1 << 8)
        #define EMMC_STATE_READ_TRANSFER_ACTIVE  (1 << 9)
        #define EMMC_STATE_BUFFER_WRITE_ENABLE   (1 << 10)
        #define EMMC_STATE_BUFFER_READ_ENABLE    (1 << 11)
        #define EMMC_STATE_CARD_INSERTED         (1 << 16)
        #define EMMC_STATE_CARD_STATE_STABLE     (1 << 17)
        #define EMMC_STATE_CARD_DETECT_PIN_LEVEL (1 << 18)
        #define EMMC_STATE_WRITE_PROTECT         (1 << 19)
        #define EMMC_STATE_CMD_LINE_SIGNAL_LVL   (1 << 24)

    uint32_t ctrl0;         // 28h
        #define EMMC_HOSTCTRL_LED_ON              (1 << 0)
        #define EMMC_HOSTCTRL_FOUR_BIT_BUS_WIDTH  (1 << 1)
        #define EMMC_HOSTCTRL_HIGHSPEED_ENABLE    (1 << 2)
        #define EMMC_PWRCTRL_SD_BUS_POWER         (1 << 8)
    uint32_t ctrl1;         // 2Ch
        #define EMMC_INTERNAL_CLOCK_ENABLE        (1 << 0)
        #define EMMC_INTERNAL_CLOCK_STABLE        (1 << 1)
        #define EMMC_SD_CLOCK_ENABLE              (1 << 2)
        #define EMMC_PROGRAMMABLE_CLOCK_GENERATOR (1 << 5)
        #define EMMC_SOFTWARE_RESET_ALL           (1 << 24)
        #define EMMC_SOFTWARE_RESET_CMD           (1 << 25)
        #define EMMC_SOFTWARE_RESET_DAT           (1 << 26)
    uint32_t irq;           // 30h
    uint32_t irqmsk;        // 34h
    uint32_t irqen;         // 38h
        #define EMMC_IRQ_CMD_CPLT         (1 << 0)
        #define EMMC_IRQ_XFER_CPLT        (1 << 1)
        #define EMMC_IRQ_BLK_GAP_EVT      (1 << 2)
        #define EMMC_IRQ_DMA              (1 << 3)
        #define EMMC_IRQ_BUFF_WRITE_READY (1 << 4)
        #define EMMC_IRQ_BUFF_READ_READY  (1 << 5)
        #define EMMC_IRQ_CARD_INSERTION   (1 << 6)
        #define EMMC_IRQ_CARD_REMOVAL     (1 << 7)
        #define EMMC_IRQ_CARD_INTERRUPT   (1 << 8)
        #define EMMC_IRQ_A                (1 << 9)
        #define EMMC_IRQ_B                (1 << 10)
        #define EMMC_IRQ_C                (1 << 11)
        #define EMMC_IRQ_RETUNING         (1 << 12)
        #define EMMC_IRQ_ERR              (1 << 15)

        #define EMMC_IRQ_ERR_CMD_TIMEOUT   (1 << 16)
        #define EMMC_IRQ_ERR_CMD_CRC       (1 << 17)
        #define EMMC_IRQ_ERR_CMD_END_BIT   (1 << 18)
        #define EMMC_IRQ_ERR_CMD_INDEX     (1 << 19)
        #define EMMC_IRQ_ERR_DAT_TIMEOUT   (1 << 20)
        #define EMMC_IRQ_ERR_DAT_CRC       (1 << 21)
        #define EMMC_IRQ_ERR_DAT_ENDBIT    (1 << 22)
        #define EMMC_IRQ_ERR_CURRENT_LIMIT (1 << 23)
        #define EMMC_IRQ_ERR_AUTO_CMD      (1 << 24)
        #define EMMC_IRQ_ERR_ADMA          (1 << 25)
        #define EMMC_IRQ_ERR_TUNING        (1 << 26)
        #define EMMC_IRQ_ERR_VS_1          (1 << 28)
        #define EMMC_IRQ_ERR_VS_2          (1 << 29)
        #define EMMC_IRQ_ERR_VS_3          (1 << 30)
        #define EMMC_IRQ_ERR_VS_4          (1 << 31)
    uint32_t ctrl2;         // 3Ch
    uint32_t caps0;         // 40h
    uint32_t caps1;         // 44h
    uint32_t maxcaps0;      // 48h
    uint32_t maxcaps1;      // 4Ch
    uint32_t forceirq;      // 50h
    uint32_t admaerr;       // 54h
    uint32_t admaaddr0;     // 58h
    uint32_t admaaddr1;     // 5Ch
    uint32_t preset[4];     // 60h
    uint8_t  resvd[112];
    uint32_t busctl;

    uint8_t _reserved_4[24];

    uint32_t slotirqversion;
        #define SDHCI_VERSION_1 0x00
        #define SDHCI_VERSION_2 0x01
        #define SDHCI_VERSION_3 0x02
} __PACKED;

typedef struct emmc {
    // Interrupts mapped here.
    mx_handle_t irq_handle;

    // Used to signal that a command has completed.
    completion_t irq_completion;

    // value of the irq register when the last irq fired (masked against the irqs
    // that were enabled at the time).
    uint32_t irq;

    // Memory mapped device registers.
    volatile struct emmc_regs* regs;

    // Device heirarchy
    mx_device_t* mxdev;
    mx_device_t* parent;

    // Held when a command or action is in progress.
    mtx_t mtx;

    // Cached base clock rate that the pi is running at.
    uint32_t base_clock;
} emmc_t;

typedef struct emmc_setup_context {
    mx_device_t* dev;
} emmc_setup_context_t;

// If any of these interrupts is asserted in the SDHCI irq register, it means
// that an error has occured.
static const uint32_t error_interrupts = (
    EMMC_IRQ_ERR |
    EMMC_IRQ_ERR_CMD_TIMEOUT |
    EMMC_IRQ_ERR_CMD_CRC |
    EMMC_IRQ_ERR_CMD_END_BIT |
    EMMC_IRQ_ERR_CMD_INDEX |
    EMMC_IRQ_ERR_DAT_TIMEOUT |
    EMMC_IRQ_ERR_DAT_CRC |
    EMMC_IRQ_ERR_DAT_ENDBIT |
    EMMC_IRQ_ERR_CURRENT_LIMIT |
    EMMC_IRQ_ERR_AUTO_CMD |
    EMMC_IRQ_ERR_ADMA |
    EMMC_IRQ_ERR_TUNING
);

// These interrupts indicate that a transfer or command has progressed normally.
static const uint32_t normal_interrupts = (
    EMMC_IRQ_CMD_CPLT |
    EMMC_IRQ_BUFF_READ_READY |
    EMMC_IRQ_BUFF_WRITE_READY
);

// Callback used to await the bcm-vc-rpc mailbox device. When the device is
// added to the watched directory, we return 1 to tell the watcher to stop
// watching.
static mx_status_t mailbox_open_cb(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return NO_ERROR;
    }

    const char bcm_vc_rpc[] = "bcm-vc-rpc";
    if (strncmp(fn, bcm_vc_rpc, sizeof(bcm_vc_rpc)) == 0) {
        return 1; // stop polling.
    }
    return NO_ERROR;
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

static int emmc_irq_thread(void *arg) {
    xprintf("emmc: entering irq thread\n");

    mx_status_t wait_res;
    emmc_t* emmc = (emmc_t*)arg;
    volatile struct emmc_regs* regs = emmc->regs;
    mx_handle_t irq_handle = emmc->irq_handle;

    while (true) {
        wait_res = mx_interrupt_wait(irq_handle);
        if (wait_res != NO_ERROR) {
            xprintf("emmc: interrupt wait failed with retcode = %d\n", wait_res);
        }

        // Obtain the IRQs that were active when the interrupt fired.
        // Only stash the IRQs that were actually enabled.
        const uint32_t irq = (regs->irq & regs->irqen);

        // Stash these IRQs so that they can be processed by the caller.
        emmc->irq = irq;

        // Acknowledge the IRQs that we stashed. IRQs are cleared by writing
        // 1s into the IRQs that fired.
        regs->irq = irq;

        // Mark this interrupt as completed.
        mx_interrupt_complete(irq_handle);

        // Signal that an IRQ happened.
        completion_signal(&emmc->irq_completion);
    }

    // Control should never really reach here.
    xprintf("emmc: irq_thread done?\n");
    return 0;
}

// Helper function that awaits an IRQ.
// Returns NO_ERROR if no error condition was detected, otherwise returns
// ERR_IO.
static mx_status_t emmc_await_irq(emmc_t* emmc) {
    mx_status_t st = completion_wait(&emmc->irq_completion, MX_TIME_INFINITE);
    completion_reset(&emmc->irq_completion);

    // Did completion wait return some kind of error?
    if (st != NO_ERROR)
        return st;

    // Was the IRQ triggered by an error interrupt?
    if (emmc->irq & error_interrupts) {
        xprintf("emmc: interrupt error = 0x%08x\n", (emmc->irq & error_interrupts));
        return ERR_IO;
    }

    return NO_ERROR;
}

static void emmc_iotxn_queue(void* ctx, iotxn_t* txn) {
    // Ensure that the offset is some multiple of the block size, we don't allow
    // writes that are partway into a block.
    if (txn->offset % SDHC_BLOCK_SIZE) {
        xprintf("sdmmc: iotxn offset not aligned to block boundary, "
               "offset =%" PRIu64", block size = %d\n", txn->offset, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // Ensure that the length of the write is some multiple of the block size.
    if (txn->length % SDHC_BLOCK_SIZE) {
        xprintf("sdmmc: iotxn length not aligned to block boundary, "
               "offset =%" PRIu64", block size = %d\n", txn->length, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    emmc_t* emmc = ctx;
    mx_status_t st;
    mtx_lock(&emmc->mtx);

    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);

    volatile struct emmc_regs* regs = emmc->regs;
    const uint32_t arg = pdata->arg;
    const uint16_t blkcnt = pdata->blockcount;
    const uint16_t blksiz = pdata->blocksize;
    uint32_t cmd = pdata->cmd;

    // Every command requires that the Command Inhibit is unset.
    uint32_t inhibit_mask = EMMC_STATE_CMD_INHIBIT;

    // Busy type commands must also wait for the DATA Inhibit to be 0 UNLESS
    // it's an abort command which can be issued with the data lines active.
    if ((cmd & SDMMC_RESP_LEN_48B) && ((cmd & SDMMC_CMD_TYPE_ABORT) == 0)) {
        inhibit_mask |= EMMC_STATE_DAT_INHIBIT;
    }

    // Wait for the inhibit masks from above to become 0 before issuing the
    // command.
    while (regs->state & inhibit_mask)
        mx_nanosleep(mx_deadline_after(MX_MSEC(1)));

    // This command has a data phase?
    if (cmd & SDMMC_RESP_DATA_PRESENT) {
        iotxn_physmap(txn);
        MX_DEBUG_ASSERT(txn->phys_count == 1);
        regs->arg2 = iotxn_phys(txn) + BCM_SDRAM_BUS_ADDR_BASE;

        iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, blkcnt * blksiz);

        if (cmd & SDMMC_CMD_MULTI_BLK)
            cmd |= SDMMC_CMD_AUTO12;
    }

    regs->blkcntsiz = (blksiz | (blkcnt << 16));

    regs->arg1 = arg;

    // Enable the appropriate interrupts.
    regs->irqmsk = error_interrupts | normal_interrupts;
    regs->irqen = error_interrupts | EMMC_IRQ_CMD_CPLT;

    // Clear any pending interrupts before starting the transaction.
    regs->irq = regs->irqen;

    // And we're off to the races!
    regs->cmd = cmd;

    if ((st = emmc_await_irq(emmc)) != NO_ERROR) {
        iotxn_complete(txn, ERR_IO, 0);
        goto exit;
    }

    // Read the response data.
    if (cmd & SDMMC_RESP_LEN_136) {
        // NOTE: This is a BCM28xx specific quirk. The bottom 8 bits of the 136
        // bit response are normally filled by 7 CRC bits and 1 reserved bit.
        // The BCM controller checks the CRC for us and strips it off in the
        // process.
        // The higher level stack expects 136B responses to be packed in a
        // certain way so we shift all the fields back to their proper offsets.
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
            regs->irqen = error_interrupts | EMMC_IRQ_BUFF_READ_READY;
        } else {
            regs->irqen = error_interrupts | EMMC_IRQ_BUFF_WRITE_READY;
        }

        // Sequentially read or write each block.
        // BCM28xx quirk: The BCM28xx appears to use its internal DMA engine to
        // perform transfers against the SD card. Normally we would use SDMA or
        // ADMA (if the part supported it). Since this part doesn't appear to
        // support either, we just use PIO.
        for (size_t blkid = 0; blkid < blkcnt; blkid++) {
            mx_status_t st;
            if ((st = emmc_await_irq(emmc)) != NO_ERROR) {
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

        if ((regs->state & EMMC_STATE_DAT_INHIBIT) == 0) {
            regs->irq = 0xffff0002;
        }
    }

    iotxn_complete(txn, NO_ERROR, bytes_copied);

exit:
    mtx_unlock(&emmc->mtx);
}

static mx_status_t emmc_set_bus_frequency(emmc_t* emmc, uint32_t target_freq) {
    const uint32_t divider = get_clock_divider(emmc->base_clock, target_freq);
    const uint8_t divider_lo = divider & 0xff;
    const uint8_t divider_hi = (divider >> 8) & 0x3;

    volatile struct emmc_regs* regs = emmc->regs;

    uint32_t iterations = 0;
    while (regs->state & (EMMC_STATE_CMD_INHIBIT | EMMC_STATE_DAT_INHIBIT)) {
        if (++iterations > 1000)
            return ERR_TIMED_OUT;

        mx_nanosleep(mx_deadline_after(MX_MSEC(1)));
    }

    // Turn off the SD clock before messing with the clock rate.
    regs->ctrl1 &= ~EMMC_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Write the new divider into the control register.
    uint32_t ctrl1 = regs->ctrl1;
    ctrl1 &= ~0xffe0;
    ctrl1 |= ((divider_lo << 8) | (divider_hi << 6));
    regs->ctrl1 = ctrl1;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Turn the SD clock back on.
    regs->ctrl1 |= EMMC_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return NO_ERROR;
}

static mx_status_t emmc_set_bus_width(emmc_t* emmc, const uint32_t new_bus_width) {
    switch (new_bus_width) {
        case 1:
            emmc->regs->ctrl0 &= ~EMMC_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
            break;
        case 4:
            emmc->regs->ctrl0 |= EMMC_HOSTCTRL_FOUR_BIT_BUS_WIDTH;
            break;
        default:
            return ERR_INVALID_ARGS;
    }

    return NO_ERROR;
}

static mx_status_t emmc_set_voltage(emmc_t* emmc, uint32_t new_voltage) {

    switch (new_voltage) {
        case SDMMC_VOLTAGE_33:
        case SDMMC_VOLTAGE_30:
        case SDMMC_VOLTAGE_18:
            break;
        default:
            return ERR_INVALID_ARGS;
    }

    volatile struct emmc_regs* regs = emmc->regs;

    // Disable the SD clock before messing with the voltage.
    regs->ctrl1 &= ~EMMC_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Wait for the DAT lines to settle
    const mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);
    while (true) {
        printf("Waiting for dat lines to go to 0\n");
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            return ERR_TIMED_OUT;
        }

        uint8_t dat_lines = ((regs->state) >> 20) & 0xf;
        if (dat_lines == 0) break;

        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }

    // Cut voltage to the card
    regs->ctrl0 &= ~EMMC_PWRCTRL_SD_BUS_POWER;

    regs->ctrl0 |= new_voltage;

    // Restore voltage to the card.
    regs->ctrl0 |= EMMC_PWRCTRL_SD_BUS_POWER;

    // Make sure our changes are acknolwedged.
    const uint32_t expected_mask = (EMMC_PWRCTRL_SD_BUS_POWER) | (new_voltage);
    if ((regs->ctrl0 & expected_mask) != expected_mask) return ERR_INTERNAL;

    // Turn the clock back on
    regs->ctrl1 |= EMMC_SD_CLOCK_ENABLE;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    return NO_ERROR;
}

static mx_status_t emmc_ioctl(void* ctx, uint32_t op,
                          const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len, size_t* out_actual) {
    emmc_t* emmc = ctx;
    uint32_t* arg;
    arg = (uint32_t*)in_buf;
    if (in_len < sizeof(*arg))
        return ERR_INVALID_ARGS;

    switch (op) {
    case IOCTL_SDMMC_SET_VOLTAGE:
        return emmc_set_voltage(emmc, *arg);
    case IOCTL_SDMMC_SET_BUS_WIDTH:
        if ((*arg != 4) && (*arg != 1))
            return ERR_INVALID_ARGS;
        return emmc_set_bus_width(emmc, *arg);
    case IOCTL_SDMMC_SET_BUS_FREQ:
        xprintf("emmc: ioctl set bus frequency to %u\n", *arg);
        return emmc_set_bus_frequency(emmc, *arg);
    }

    return ERR_NOT_SUPPORTED;
}

static void emmc_unbind(void* ctx) {
    emmc_t* emmc = ctx;
    device_remove(emmc->mxdev);
}

static void emmmc_release(void* ctx) {
    emmc_t* emmc = ctx;
    free(emmc);
}

static mx_protocol_device_t emmc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = emmc_iotxn_queue,
    .ioctl = emmc_ioctl,
    .unbind = emmc_unbind,
    .release = emmmc_release,
};

// Async thread that binds the device.
static int emmc_bootstrap_thread(void *arg) {
    mx_status_t st = NO_ERROR;
    emmc_t* emmc = NULL;

    // Extract all context from our context argument then free the context
    // structure.
    assert(arg);
    emmc_setup_context_t* ctx = (emmc_setup_context_t*)arg;
    mx_device_t* dev = ctx->dev;
    free(arg);

    // Map the Device Registers so that we can perform MMIO against the device.
    volatile struct emmc_regs* regs;
    st = mx_mmap_device_memory(get_root_resource(), SDMMC_PAGE_START,
                               (uint32_t)SDMMC_PAGE_SIZE,
                               MX_CACHE_POLICY_UNCACHED_DEVICE,
                               (uintptr_t*)(&regs));
    if (st != NO_ERROR) {
        xprintf("emmc: failed to mmap device memory, retcode = %d\n", st);
        goto out;
    }

    // Create an interrupt handle for this device.
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    irq_handle = mx_interrupt_create(get_root_resource(),
                                     INTERRUPT_VC_ARASANSDIO,
                                     MX_FLAG_REMAP_IRQ);
    if (irq_handle < 0) {
        xprintf("emmc: failed to create interrupt handle, handle = %d\n",
                irq_handle);
        st = irq_handle;
        goto out;
    }

    // Allocate the device object and fill it in with all the relevent data
    // structures.
    emmc = calloc(1, sizeof(*emmc));
    if (!emmc) {
        xprintf("emmc: failed to allocate device, no memory!\n");
        st = ERR_NO_MEMORY;
        goto out;
    }

    mx_device_t* bus_dev;
    bcm_bus_protocol_t* bus_proto;
    st = platform_device_find_protocol(dev, MX_PROTOCOL_BCM_BUS, &bus_dev, (void**)&bus_proto);
    if (st != NO_ERROR) {
        printf("emmc_bootstrap_thread could not find MX_PROTOCOL_BCM_BUS\n");
        goto out;
    }

    // Stash relevent data structrues.
    emmc->irq_handle = irq_handle;
    emmc->irq_completion = COMPLETION_INIT;
    emmc->regs = regs;
    emmc->parent = dev;

    // Ensure that we're SDv3 or above.
    const uint16_t vrsn = (regs->slotirqversion >> 16) & 0xff;
    if (vrsn < SDHCI_VERSION_3) {
        xprintf("emmc: SD version is %u, only version %u and above are "
                "supported\n", vrsn, SDHCI_VERSION_3);
        st = ERR_NOT_SUPPORTED;
        goto out;
    }

    // Reset the controller.
    uint32_t ctrl1 = regs->ctrl1;

    // Perform a software reset against both the DAT and CMD interface.
    ctrl1 |= EMMC_SOFTWARE_RESET_ALL;

    // Disable both clocks.
    ctrl1 &= ~(EMMC_INTERNAL_CLOCK_ENABLE | EMMC_SD_CLOCK_ENABLE);

    // Write the register back to the device.
    regs->ctrl1 = ctrl1;

    // Wait for th reset to take place. The reset is comleted when all three
    // of the following flags are reset.
    const uint32_t target_mask = (EMMC_SOFTWARE_RESET_ALL |
                                  EMMC_SOFTWARE_RESET_CMD |
                                  EMMC_SOFTWARE_RESET_DAT);
    mx_time_t deadline =
        mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);

    while (true) {
        if (((regs->ctrl1) & target_mask) == 0)
            break;

        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            xprintf("emmc: timed out while waiting for reset\n");
            st = ERR_TIMED_OUT;
            goto out;
        }
    }

    // Configure the clock.
    uint32_t base_clock = 0;
    const uint32_t bcm28xX_core_clock_id = 1;
    st = bus_proto->get_clock_rate(bus_dev, bcm28xX_core_clock_id, &base_clock);
     if (st < 0 || base_clock == 0) {
        xprintf("emmc: failed to get base clock rate, retcode = %d\n", st);
        goto out;
    }

    ctrl1 = regs->ctrl1;
    ctrl1 |= EMMC_INTERNAL_CLOCK_ENABLE;

    emmc->base_clock = base_clock;

    // SDHCI Versions 1.00 and 2.00 handle the clock divider slightly
    // differently compared to SDHCI version 3.00. Since this driver doesn't
    // support SDHCI versions < 3.00, we ignore this incongruency for now.
    //
    // V3.00 supports a 10 bit divider where the SD clock frequency is defined
    // as F/(2*D) where F is the base clock frequency and D is the divider.
    const uint32_t divider = get_clock_divider(base_clock, SD_FREQ_SETUP_HZ);
    const uint8_t divider_lo = divider & 0xff;
    const uint8_t divider_hi = (divider >> 8) & 0x3;
    ctrl1 |= ((divider_lo << 8) | (divider_hi << 6));

    // Set the command timeout.
    ctrl1 |= (0xe << 16);

    // Write back the clock frequency, command timeout and clock enable bits.
    regs->ctrl1 = ctrl1;

    // Wait for the clock to stabilize.
    deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_SEC(1);
    while (true) {
        if (((regs->ctrl1) & EMMC_INTERNAL_CLOCK_STABLE) != 0)
            break;

        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline) {
            xprintf("emmc: Clock did not stabilize in time\n");
            st = ERR_TIMED_OUT;
            goto out;
        }
    }

    // Enable the SD clock.
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));
    ctrl1 |= regs->ctrl1;
    ctrl1 |= EMMC_SD_CLOCK_ENABLE;
    regs->ctrl1 = ctrl1;
    mx_nanosleep(mx_deadline_after(MX_MSEC(2)));

    // Disable all interrupts before we create the IRQ thread.
    regs->irqen = 0;
    regs->irq = 0xffffffff;

    // Create a thread to handle IRQs.
    thrd_t irq_thrd;
    int thrd_rc = thrd_create_with_name(&irq_thrd, emmc_irq_thread, emmc,
                                        "emmc_irq_thread");
    if (thrd_rc != thrd_success) {
        xprintf("emmc: failed to create irq thread\n");
        goto out;
    }
    thrd_detach(irq_thrd);

    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bcm-emmc",
        .ctx = emmc,
        .ops = &emmc_device_proto,
        .proto_id = MX_PROTOCOL_SDMMC,
    };

    st = device_add(emmc->parent, &args, &emmc->mxdev);
    if (st != NO_ERROR) {
        goto out;
    }

    // Everything went okay, exit the bootstrap thread!
    return 0;

out:
    if (emmc)
        free(emmc);

    device_unbind(dev);

    // If we're in the error path, make sure the error retcode is set.
    assert(st != NO_ERROR);

    xprintf("emmc: there was an error while trying to bind the emmc device. "
            "Error retcode = %d\n", st);
    return -1;
}

static mx_status_t emmc_bind(void* drv_ctx, mx_device_t* dev, void** cookie) {
    // Create a context to pass bind variables to the bootstrap thread.
    emmc_setup_context_t* ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return ERR_NO_MEMORY;
    ctx->dev = dev;

    // Create a bootstrap thread.
    thrd_t bootstrap_thrd;
    int thrd_rc = thrd_create_with_name(&bootstrap_thrd,
                                        emmc_bootstrap_thread, ctx,
                                        "emmc_bootstrap_thread");
    if (thrd_rc != thrd_success) {
        free(ctx);
        return thrd_status_to_mx_status(thrd_rc);
    }

    thrd_detach(bootstrap_thrd);
    return NO_ERROR;
}

static mx_driver_ops_t emmc_dwc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = emmc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
MAGENTA_DRIVER_BEGIN(bcm_emmc, emmc_dwc_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BROADCOMM_EMMC),
MAGENTA_DRIVER_END(bcm_emmc)
// clang-format on
