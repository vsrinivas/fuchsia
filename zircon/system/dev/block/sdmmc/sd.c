// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/sdmmc.h>

#include "sdmmc.h"

// If this bit is set in the Operating Conditions Register, then we know that
// the card is a SDHC (high capacity) card.
#define OCR_SDHC      0xc0000000

// The "STRUCTURE" field of the "Card Specific Data" register defines the
// version of the structure and how to interpret the rest of the bits.
#define CSD_STRUCT_V1 0x0
#define CSD_STRUCT_V2 0x1

zx_status_t sdmmc_probe_sd(sdmmc_device_t* dev) {
    // TODO comment ths out for now, we do not have SD card support
    return ZX_ERR_NOT_SUPPORTED;
#if 0
    // Issue the SEND_IF_COND command, this will tell us that we can talk to
    // the card correctly and it will also tell us if the voltage range that we
    // have supplied has been accepted.
    zx_status_t st = sd_send_if_cond(dev);
    if (st != ZX_OK) {
        return st;
    } else {
        return ZX_OK;
    }

    // Get the operating conditions from the card.
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_APP_CMD, 0, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: SDMMC_APP_CMD failed, retcode = %d\n", st);
        goto err;
    }
    if ((sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SD_SEND_OP_COND, 0, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: SDMMC_SD_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    int attempt = 0;
    const int max_attempts = 10;
    bool card_supports_18v_signalling = false;
    while (true) {
        // Ask for high speed.
        const uint32_t flags = (1 << 30)  | 0x00ff8000 | (1 << 24);
        if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_APP_CMD, 0, setup_txn)) != ZX_OK) {
            zxlogf(ERROR, "sd: APP_CMD failed with retcode = %d\n", st);
            goto err;
        }
        if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SD_SEND_OP_COND, flags, setup_txn)) != ZX_OK) {
            zxlogf(ERROR, "sd: SD_SEND_OP_COND failed with retcode = %d\n", st);
            goto err;
        }

        const uint32_t ocr = pdata->response[0];
        if (ocr & (1 << 31)) {
            if (!(ocr & OCR_SDHC)) {
                // Card is not an SDHC card. We currently don't support this.
                zxlogf(ERROR, "sd: unsupported card type, must use sdhc card\n");
                goto err;
            }
            card_supports_18v_signalling = !!((ocr >> 24) & 0x1);
            break;
        }

        if (++attempt == max_attempts) {
            zxlogf(ERROR, "sd: too many attempt trying to negotiate card OCR\n");
            goto err;
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    }

    st = sdmmc_set_bus_freq(&sdmmc->host, 25000000);
    if (st != ZX_OK) {
        // This is non-fatal but the card will run slowly.
        zxlogf(ERROR, "sd: failed to increase bus frequency.\n");
    }

    // Try to switch the bus voltage to 1.8v
    if (card_supports_18v_signalling) {
        if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_VOLTAGE_SWITCH, 0, setup_txn)) != ZX_OK) {
            zxlogf(ERROR, "sd: failed to send switch voltage command to card, "
                    "retcode = %d\n", st);
            goto err;
        }

        st = sdmmc_set_signal_voltage(&sdmmc->host, SDMMC_VOLTAGE_180);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sd: Card supports 1.8v signalling but was unable to "
                    "switch to 1.8v mode, retcode = %d\n", st);
            goto err;
        }
    }

    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_ALL_SEND_CID, 0, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: ALL_SEND_CID failed with retcode = %d\n", st);
        goto err;
    }

    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SEND_RELATIVE_ADDR, 0, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with retcode = %d\n", st);
        goto err;
    }

    sdmmc->type = SDMMC_TYPE_SD;
    sdmmc->rca = (pdata->response[0] >> 16) & 0xffff;
    if (pdata->response[0] & 0xe000) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with resp = %d\n",
                (pdata->response[0] & 0xe000));
        st = ZX_ERR_INTERNAL;
        goto err;
    }
    if ((pdata->response[0] & (1u << 8)) == 0) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed. Card not ready.\n");
        st = ZX_ERR_INTERNAL;
        goto err;
    }

    // Determine the size of the card.
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SEND_CSD, sdmmc->rca << 16, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: failed to send app cmd, retcode = %d\n", st);
        goto err;
    }

    // For now we only support SDHC cards. These cards must have a CSD type = 1,
    // since CSD type 0 is unable to support SDHC sized cards.
    uint8_t csd_structure = (pdata->response[0] >> 30) & 0x3;
    if (csd_structure != CSD_STRUCT_V2) {
        zxlogf(ERROR, "sd: unsupported card type, expected CSD version = %d, "
                "got version %d\n", CSD_STRUCT_V2, csd_structure);
        goto err;
    }

    const uint32_t c_size = ((pdata->response[2] >> 16) |
                             (pdata->response[1] << 16)) & 0x3fffff;
    sdmmc->capacity = (c_size + 1ul) * 512ul * 1024ul;
    printf("sd: found card with capacity = %"PRIu64"B\n", sdmmc->capacity);

    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SELECT_CARD, sdmmc->rca << 16, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: SELECT_CARD failed with retcode = %d\n", st);
        goto err;
    }

    pdata->blockcount = 1;
    pdata->blocksize = 8;
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: APP_CMD failed with retcode = %d\n", st);
        goto err;
    }
    if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SEND_SCR, 0, setup_txn)) != ZX_OK) {
        zxlogf(ERROR, "sd: SEND_SCR failed with retcode = %d\n", st);
        goto err;
    }
    pdata->blockcount = 512;
    pdata->blocksize = 1;

    uint32_t scr;
    iotxn_copyfrom(setup_txn, &scr, sizeof(scr), 0);
    scr = be32toh(scr);

    // If this card supports 4 bit mode, then put it into 4 bit mode.
    const uint32_t supported_bus_widths = (scr >> 16) & 0xf;
    if (supported_bus_widths & 0x4) {
        do {
            // First tell the card to go into four bit mode:
            if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != ZX_OK) {
                zxlogf(ERROR, "sd: failed to send app cmd, retcode = %d\n", st);
                break;
            }
            if ((st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_SET_BUS_WIDTH, 2, setup_txn)) != ZX_OK) {
                zxlogf(ERROR, "sd: failed to set card bus width, retcode = %d\n", st);
                break;
            }
            st = sdmmc_set_bus_width(&sdmmc->host, SDMMC_BUS_WIDTH_4);
            if (st != ZX_OK) {
                zxlogf(ERROR, "sd: failed to set host bus width, retcode = %d\n", st);
            }
        } while (false);
    }
#endif
}

