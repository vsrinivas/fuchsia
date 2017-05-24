// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standard Includes
#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/sdmmc.h>

// Magenta Includes
#include <magenta/threads.h>
#include <sync/completion.h>

// If this bit is set in the Operating Conditions Register, then we know that
// the card is a SDHC (high capacity) card.
#define OCR_SDHC      0xc0000000

// The "STRUCTURE" field of the "Card Specific Data" register defines the
// version of the structure and how to interpret the rest of the bits.
#define CSD_STRUCT_V1 0x0
#define CSD_STRUCT_V2 0x1

// Various transfer states that the card can be in.
#define SDMMC_STATE_TRAN 0x4
#define SDMMC_STATE_RECV 0x5
#define SDMMC_STATE_DATA 0x6

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// Device structure.
typedef struct sdmmc {
    mx_device_t* mxdev;
    mx_device_t* sdmmc_mxdev;
    uint16_t rca;
    uint64_t capacity;
    block_callbacks_t* callbacks;
} sdmmc_t;

static void sdmmc_txn_cplt(iotxn_t* request, void* cookie) {
    completion_signal((completion_t*)cookie);
};

static mx_status_t sdmmc_do_command(mx_device_t* dev, const uint32_t cmd,
                                    const uint32_t arg, iotxn_t* txn) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    pdata->cmd = cmd;
    pdata->arg = arg;

    completion_t cplt = COMPLETION_INIT;
    txn->complete_cb = sdmmc_txn_cplt;
    txn->cookie = &cplt;

    iotxn_queue(dev, txn);

    completion_wait(&cplt, MX_TIME_INFINITE);

    return txn->status;
}

static mx_off_t sdmmc_get_size(void* ctx) {
    sdmmc_t* sdmmc = ctx;
    return sdmmc->capacity;
}

static void sdmmc_get_info(block_info_t* info, void* ctx) {
    memset(info, 0, sizeof(*info));
    // Since we only support SDHC cards, the blocksize must be the SDHC
    // blocksize.
    info->block_size = SDHC_BLOCK_SIZE;
    info->block_count = sdmmc_get_size(ctx) / SDHC_BLOCK_SIZE;
}

static mx_status_t sdmmc_ioctl(void* ctx, uint32_t op, const void* cmd,
                               size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ERR_BUFFER_TOO_SMALL;
        sdmmc_get_info(info, ctx);
        *out_actual = sizeof(*info);
        return NO_ERROR;
    }
    case IOCTL_BLOCK_GET_NAME: {
        return ERR_NOT_SUPPORTED;
    }
    case IOCTL_DEVICE_SYNC: {
        return ERR_NOT_SUPPORTED;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
    return 0;
}

static void sdmmc_unbind(void* ctx) {
    sdmmc_t* sdmmc = ctx;
    device_remove(sdmmc->mxdev);
}

static void sdmmc_release(void* ctx) {
    sdmmc_t* sdmmc = ctx;
    free(sdmmc);
}

static void sdmmc_iotxn_queue(void* ctx, iotxn_t* txn) {
    if (txn->offset % SDHC_BLOCK_SIZE) {
        xprintf("sdmmc: iotxn offset not aligned to block boundary, "
                "offset =%" PRIu64 ", block size = %d\n",
                txn->offset, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    if (txn->length % SDHC_BLOCK_SIZE) {
        xprintf("sdmmc: iotxn length not aligned to block boundary, "
                "offset =%" PRIu64 ", block size = %d\n",
                txn->length, SDHC_BLOCK_SIZE);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    iotxn_t* emmc_txn = NULL;
    sdmmc_t* sdmmc = ctx;
    mx_device_t* sdmmc_mxdev = sdmmc->sdmmc_mxdev;
    uint32_t cmd = 0;

    // Figure out which SD command we need to issue.
    switch(txn->opcode) {
        case IOTXN_OP_READ:
            if (txn->length > SDHC_BLOCK_SIZE) {
                cmd = SDMMC_READ_MULTIPLE_BLOCK;
            } else {
                cmd = SDMMC_READ_BLOCK;
            }
            break;
        case IOTXN_OP_WRITE:
            if (txn->length > SDHC_BLOCK_SIZE) {
                cmd = SDMMC_WRITE_MULTIPLE_BLOCK;
            } else {
                cmd = SDMMC_WRITE_BLOCK;
            }
            break;
        default:
            // Invalid opcode?
            iotxn_complete(txn, ERR_INVALID_ARGS, 0);
            return;
    }

    if (iotxn_alloc(&emmc_txn, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, txn->length) != NO_ERROR) {
        xprintf("sdmmc: error allocating emmc iotxn\n");
        iotxn_complete(txn, ERR_INTERNAL, 0);
        return;
    }
    emmc_txn->opcode = txn->opcode;
    emmc_txn->flags = txn->flags;
    emmc_txn->offset = txn->offset;
    emmc_txn->length = txn->length;
    emmc_txn->protocol = MX_PROTOCOL_SDMMC;
    sdmmc_protocol_data_t* pdata = iotxn_pdata(emmc_txn, sdmmc_protocol_data_t);

    uint8_t current_state;
    const size_t max_attempts = 10;
    size_t attempt = 0;
    for (; attempt <= max_attempts; attempt++) {
        mx_status_t rc = sdmmc_do_command(sdmmc_mxdev, SDMMC_SEND_STATUS,
                                          sdmmc->rca << 16, emmc_txn);
        if (rc != NO_ERROR) {
            iotxn_complete(txn, rc, 0);
            goto out;
        }

        current_state = (pdata->response[0] >> 9) & 0xf;

        if (current_state == SDMMC_STATE_RECV) {
            rc = sdmmc_do_command(sdmmc_mxdev, SDMMC_STOP_TRANSMISSION, 0, emmc_txn);
            continue;
        } else if (current_state == SDMMC_STATE_TRAN) {
            break;
        }

        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }

    if (attempt == max_attempts) {
        // Too many retries, fail.
        iotxn_complete(txn, ERR_BAD_STATE, 0);
        goto out;
    }

    // Which block to operate against.
    const uint32_t blkid = emmc_txn->offset / SDHC_BLOCK_SIZE;

    pdata->blockcount = txn->length / SDHC_BLOCK_SIZE;
    pdata->blocksize = SDHC_BLOCK_SIZE;

    void* buffer;
    size_t bytes_processed = 0;
    if (txn->opcode == IOTXN_OP_WRITE) {
        iotxn_mmap(txn, &buffer);
        iotxn_copyto(emmc_txn, buffer, txn->length, 0);
        bytes_processed = txn->length;
    }

    mx_status_t rc = sdmmc_do_command(sdmmc_mxdev, cmd, blkid, emmc_txn);
    if (rc != NO_ERROR) {
        iotxn_complete(txn, rc, 0);
    }

    if (txn->opcode == IOTXN_OP_READ) {
        bytes_processed = MIN(emmc_txn->actual, txn->length);
        iotxn_mmap(emmc_txn, &buffer);
        iotxn_copyto(txn, buffer, bytes_processed, 0);
    }

    iotxn_complete(txn, NO_ERROR, bytes_processed);

out:
    if (emmc_txn)
        iotxn_release(emmc_txn);
}

// Block device protocol.
static mx_protocol_device_t sdmmc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sdmmc_ioctl,
    .unbind = sdmmc_unbind,
    .release = sdmmc_release,
    .iotxn_queue = sdmmc_iotxn_queue,
    .get_size = sdmmc_get_size,
};

static void sdmmc_block_set_callbacks(mx_device_t* dev, block_callbacks_t* cb) {
    sdmmc_t* device = dev->ctx;
    device->callbacks = cb;
}

static void sdmmc_block_get_info(mx_device_t* dev, block_info_t* info) {
    sdmmc_t* device = dev->ctx;
    sdmmc_get_info(info, device);
}

static void sdmmc_block_complete(iotxn_t* txn, void* cookie) {
    sdmmc_t* dev;
    memcpy(&dev, txn->extra, sizeof(sdmmc_t*));
    dev->callbacks->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void block_do_txn(sdmmc_t* dev, uint32_t opcode, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_info_t info;
    sdmmc_get_info(&info, dev);

    if ((dev_offset % info.block_size) || (length % info.block_size)) {
        dev->callbacks->complete(cookie, ERR_INVALID_ARGS);
        return;
    }
    uint64_t size = info.block_size * info.block_count;
    if ((dev_offset >= size) || (length >= (size - dev_offset))) {
        dev->callbacks->complete(cookie, ERR_OUT_OF_RANGE);
        return;
    }

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length)) != NO_ERROR) {
        dev->callbacks->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->length = length;
    txn->offset = dev_offset;
    txn->complete_cb = sdmmc_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &dev, sizeof(sdmmc_t*));
    iotxn_queue(dev->mxdev, txn);
}

static void sdmmc_block_read(mx_device_t* dev, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn((sdmmc_t*)dev->ctx, IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

static void sdmmc_block_write(mx_device_t* dev, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn((sdmmc_t*)dev->ctx, IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

// Block core protocol
static block_ops_t sdmmc_block_ops = {
    .set_callbacks = sdmmc_block_set_callbacks,
    .get_info = sdmmc_block_get_info,
    .read = sdmmc_block_read,
    .write = sdmmc_block_write,
};

static int sdmmc_bootstrap_thread(void* arg) {
    mx_device_t* dev = arg;

    mx_status_t st;
    sdmmc_t* sdmmc = NULL;
    iotxn_t* setup_txn = NULL;


    // Allocate the device.
    sdmmc = calloc(1, sizeof(*sdmmc));
    if (!sdmmc) {
        xprintf("sdmmc: no memory to allocate sdmmc device!\n");
        goto err;
    }

    // Allocate a single iotxn that we use to bootstrap the card with.
    if ((st = iotxn_alloc(&setup_txn, IOTXN_ALLOC_CONTIGUOUS, SDHC_BLOCK_SIZE)) != NO_ERROR) {
        xprintf("sdmmc: failed to allocate iotxn for setup, rc = %d\n", st);
        goto err;
    }

    // Get the protocol data from the iotxn. We use this to pass the command
    // type and command arguments to the EMMC driver.
    sdmmc_protocol_data_t* pdata =
        iotxn_pdata(setup_txn, sdmmc_protocol_data_t);

    // Reset the card. No matter what state the card is in, issuing the
    // GO_IDLE_STATE command will put the card into the idle state.
    if ((st = sdmmc_do_command(dev, SDMMC_GO_IDLE_STATE, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SDMMC_GO_IDLE_STATE failed, retcode = %d\n", st);
        goto err;
    }

    // Issue the SEND_IF_COND command, this will tell us that we can talk to
    // the card correctly and it will also tell us if the voltage range that we
    // have supplied has been accepted.
    if ((st = sdmmc_do_command(dev, SDMMC_SEND_IF_COND, 0x1aa, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SDMMC_SEND_IF_COND failed, retcode = %d\n", st);
        goto err;
    }
    if ((pdata->response[0] & 0xFFF) != 0x1aa) {
        // The card should have replied with the pattern that we sent.
        xprintf("sdmmc: SDMMC_SEND_IF_COND got bad reply = %"PRIu32"\n",
                pdata->response[0]);
        goto err;
    }

    // Get the operating conditions from the card.
    if ((st = sdmmc_do_command(dev, SDMMC_APP_CMD, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SDMMC_APP_CMD failed, retcode = %d\n", st);
        goto err;
    }
    if ((sdmmc_do_command(dev, SDMMC_SD_SEND_OP_COND, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SDMMC_SD_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    int attempt = 0;
    const int max_attempts = 10;
    bool card_supports_18v_signalling = false;
    while (true) {
        // Ask for high speed.
        const uint32_t flags = (1 << 30)  | 0x00ff8000 | (1 << 24);
        if ((st = sdmmc_do_command(dev, SDMMC_APP_CMD, 0, setup_txn)) != NO_ERROR) {
            xprintf("sdmmc: APP_CMD failed with retcode = %d\n", st);
            goto err;
        }
        if ((st = sdmmc_do_command(dev, SDMMC_SD_SEND_OP_COND, flags, setup_txn)) != NO_ERROR) {
            xprintf("sdmmc: SD_SEND_OP_COND failed with retcode = %d\n", st);
            goto err;
        }

        const uint32_t ocr = pdata->response[0];
        if (ocr & (1 << 31)) {
            if (!(ocr & OCR_SDHC)) {
                // Card is not an SDHC card. We currently don't support this.
                xprintf("sdmmc: unsupported card type, must use sdhc card\n");
                goto err;
            }
            card_supports_18v_signalling = !!((ocr >> 24) & 0x1);
            break;
        }

        if (++attempt == max_attempts) {
            xprintf("sdmmc: too many attempt trying to negotiate card OCR\n");
            goto err;
        }

        mx_nanosleep(mx_deadline_after(MX_MSEC(5)));
    }

    uint32_t new_bus_frequency = 25000000;
    st = device_op_ioctl(dev, IOCTL_SDMMC_SET_BUS_FREQ, &new_bus_frequency,
                         sizeof(new_bus_frequency), NULL, 0, NULL);
    if (st != NO_ERROR) {
        // This is non-fatal but the card will run slowly.
        xprintf("sdmmc: failed to increase bus frequency.\n");
    }

    // Try to switch the bus voltage to 1.8v
    if (card_supports_18v_signalling) {
        if ((st = sdmmc_do_command(dev, SDMMC_VOLTAGE_SWITCH, 0, setup_txn)) != NO_ERROR) {
            xprintf("sdmmc: failed to send switch voltage command to card, "
                    "retcode = %d\n", st);
            goto err;
        }

        const uint32_t new_voltage = SDMMC_VOLTAGE_18;
        st = device_op_ioctl(dev, IOCTL_SDMMC_SET_VOLTAGE, &new_voltage,
                             sizeof(new_voltage), NULL, 0, NULL);
        if (st != NO_ERROR) {
            xprintf("sdmmc: Card supports 1.8v signalling but was unable to "
                    "switch to 1.8v mode, retcode = %d\n", st);
            goto err;
        }
    }

    if ((st = sdmmc_do_command(dev, SDMMC_ALL_SEND_CID, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: ALL_SEND_CID failed with retcode = %d\n", st);
        goto err;
    }

    if ((st = sdmmc_do_command(dev, SDMMC_SEND_RELATIVE_ADDR, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed with retcode = %d\n", st);
        goto err;
    }

    sdmmc->rca = (pdata->response[0] >> 16) & 0xffff;
    if (pdata->response[0] & 0xe000) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed with resp = %d\n",
                (pdata->response[0] & 0xe000));
        st = ERR_INTERNAL;
        goto err;
    }
    if ((pdata->response[0] & (1u << 8)) == 0) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed. Card not ready.\n");
        st = ERR_INTERNAL;
        goto err;
    }

    // Determine the size of the card.
    if ((st = sdmmc_do_command(dev, SDMMC_SEND_CSD, sdmmc->rca << 16, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: failed to send app cmd, retcode = %d\n", st);
        goto err;
    }

    // For now we only support SDHC cards. These cards must have a CSD type = 1,
    // since CSD type 0 is unable to support SDHC sized cards.
    uint8_t csd_structure = (pdata->response[0] >> 30) & 0x3;
    if (csd_structure != CSD_STRUCT_V2) {
        xprintf("sdmmc: unsupported card type, expected CSD version = %d, "
                "got version %d\n", CSD_STRUCT_V2, csd_structure);
        goto err;
    }

    const uint32_t c_size = ((pdata->response[2] >> 16) |
                             (pdata->response[1] << 16)) & 0x3fffff;
    sdmmc->capacity = (c_size + 1ul) * 512ul * 1024ul;
    xprintf("sdmmc: found card with capacity = %"PRIu64"B\n", sdmmc->capacity);

    if ((st = sdmmc_do_command(dev, SDMMC_SELECT_CARD, sdmmc->rca << 16, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SELECT_CARD failed with retcode = %d\n", st);
        goto err;
    }

    pdata->blockcount = 1;
    pdata->blocksize = 8;
    if ((st = sdmmc_do_command(dev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: APP_CMD failed with retcode = %d\n", st);
        goto err;
    }
    if ((st = sdmmc_do_command(dev, SDMMC_SEND_SCR, 0, setup_txn)) != NO_ERROR) {
        xprintf("sdmmc: SEND_SCR failed with retcode = %d\n", st);
        goto err;
    }
    pdata->blockcount = 1;
    pdata->blocksize = 512;

    uint32_t scr;
    iotxn_copyfrom(setup_txn, &scr, sizeof(scr), 0);
    scr = be32toh(scr);

    // If this card supports 4 bit mode, then put it into 4 bit mode.
    const uint32_t supported_bus_widths = (scr >> 16) & 0xf;
    if (supported_bus_widths & 0x4) {

        do {
            // First tell the card to go into four bit mode:
            if ((st = sdmmc_do_command(dev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != NO_ERROR) {
                xprintf("sdmmc: failed to send app cmd, retcode = %d\n", st);
                break;
            }
            if ((st = sdmmc_do_command(dev, SDMMC_SET_BUS_WIDTH, 2, setup_txn)) != NO_ERROR) {
                xprintf("sdmmc: failed to set card bus width, retcode = %d\n", st);
                break;
            }
            const uint32_t new_bus_width = 4;
            st = device_op_ioctl(dev, IOCTL_SDMMC_SET_BUS_WIDTH, &new_bus_width,
                                 sizeof(new_bus_width), NULL, 0, NULL);
            if (st != NO_ERROR) {
                xprintf("sdmmc: failed to set host bus width, retcode = %d\n", st);
            }
        } while (false);
    }

    sdmmc->sdmmc_mxdev = dev;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sdmmc",
        .ctx = sdmmc,
        .ops = &sdmmc_device_proto,
        .proto_id = MX_PROTOCOL_BLOCK_CORE,
        .proto_ops = &sdmmc_block_ops,
    };

    st = device_add(dev, &args, &sdmmc->mxdev);
    if (st != NO_ERROR) {
         goto err;
    }

    xprintf("sdmmc: bind success!\n");

    return 0;
err:
    if (sdmmc) {
        free(sdmmc);
    }

    if (setup_txn)
        iotxn_release(setup_txn);

    return -1;
}

static mx_status_t sdmmc_bind(void* ctx, mx_device_t* dev, void** cookie) {
    // Create a bootstrap thread.
    thrd_t bootstrap_thrd;
    int thrd_rc = thrd_create_with_name(&bootstrap_thrd,
                                        sdmmc_bootstrap_thread, dev,
                                        "sdmmc_bootstrap_thread");
    if (thrd_rc != thrd_success) {
        return thrd_status_to_mx_status(thrd_rc);
    }

    thrd_detach(bootstrap_thrd);
    return NO_ERROR;
}

static mx_driver_ops_t sdmmc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sdmmc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
MAGENTA_DRIVER_BEGIN(sdmmc, sdmmc_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_SDMMC),
MAGENTA_DRIVER_END(sdmmc)
// clang-format on
