// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/protocol/sdmmc.h>

#include "sdmmc.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// If this bit is set in the Operating Conditions Register, then we know that
// the card is a SDHC (high capacity) card.
#define OCR_SDHC      0xc0000000

// The "STRUCTURE" field of the "Card Specific Data" register defines the
// version of the structure and how to interpret the rest of the bits.
#define CSD_STRUCT_V1 0x0
#define CSD_STRUCT_V2 0x1

mx_status_t sdmmc_probe_sd(sdmmc_t* sdmmc, iotxn_t* setup_txn) {
    mx_status_t st;

    // Get the protocol data from the iotxn. We use this to pass the command
    // type and command arguments to the EMMC driver.
    sdmmc_protocol_data_t* pdata =
        iotxn_pdata(setup_txn, sdmmc_protocol_data_t);

    // Issue the SEND_IF_COND command, this will tell us that we can talk to
    // the card correctly and it will also tell us if the voltage range that we
    // have supplied has been accepted.
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SEND_IF_COND, 0x1aa, setup_txn)) != MX_OK) {
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
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_APP_CMD, 0, setup_txn)) != MX_OK) {
        xprintf("sdmmc: SDMMC_APP_CMD failed, retcode = %d\n", st);
        goto err;
    }
    if ((sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SD_SEND_OP_COND, 0, setup_txn)) != MX_OK) {
        xprintf("sdmmc: SDMMC_SD_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    int attempt = 0;
    const int max_attempts = 10;
    bool card_supports_18v_signalling = false;
    while (true) {
        // Ask for high speed.
        const uint32_t flags = (1 << 30)  | 0x00ff8000 | (1 << 24);
        if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_APP_CMD, 0, setup_txn)) != MX_OK) {
            xprintf("sdmmc: APP_CMD failed with retcode = %d\n", st);
            goto err;
        }
        if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SD_SEND_OP_COND, flags, setup_txn)) != MX_OK) {
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
    st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_FREQ, &new_bus_frequency,
                      sizeof(new_bus_frequency), NULL, 0, NULL);
    if (st != MX_OK) {
        // This is non-fatal but the card will run slowly.
        xprintf("sdmmc: failed to increase bus frequency.\n");
    }

    // Try to switch the bus voltage to 1.8v
    if (card_supports_18v_signalling) {
        if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_VOLTAGE_SWITCH, 0, setup_txn)) != MX_OK) {
            xprintf("sdmmc: failed to send switch voltage command to card, "
                    "retcode = %d\n", st);
            goto err;
        }

        const uint32_t new_voltage = SDMMC_SIGNAL_VOLTAGE_180;
        st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_SIGNAL_VOLTAGE, &new_voltage,
                          sizeof(new_voltage), NULL, 0, NULL);
        if (st != MX_OK) {
            xprintf("sdmmc: Card supports 1.8v signalling but was unable to "
                    "switch to 1.8v mode, retcode = %d\n", st);
            goto err;
        }
    }

    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_ALL_SEND_CID, 0, setup_txn)) != MX_OK) {
        xprintf("sdmmc: ALL_SEND_CID failed with retcode = %d\n", st);
        goto err;
    }

    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SEND_RELATIVE_ADDR, 0, setup_txn)) != MX_OK) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed with retcode = %d\n", st);
        goto err;
    }

    sdmmc->type = SDMMC_TYPE_SD;
    sdmmc->rca = (pdata->response[0] >> 16) & 0xffff;
    if (pdata->response[0] & 0xe000) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed with resp = %d\n",
                (pdata->response[0] & 0xe000));
        st = MX_ERR_INTERNAL;
        goto err;
    }
    if ((pdata->response[0] & (1u << 8)) == 0) {
        xprintf("sdmmc: SEND_RELATIVE_ADDR failed. Card not ready.\n");
        st = MX_ERR_INTERNAL;
        goto err;
    }

    // Determine the size of the card.
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SEND_CSD, sdmmc->rca << 16, setup_txn)) != MX_OK) {
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
    printf("sdmmc: found card with capacity = %"PRIu64"B\n", sdmmc->capacity);

    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SELECT_CARD, sdmmc->rca << 16, setup_txn)) != MX_OK) {
        xprintf("sdmmc: SELECT_CARD failed with retcode = %d\n", st);
        goto err;
    }

    pdata->blockcount = 1;
    pdata->blocksize = 8;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != MX_OK) {
        xprintf("sdmmc: APP_CMD failed with retcode = %d\n", st);
        goto err;
    }
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SEND_SCR, 0, setup_txn)) != MX_OK) {
        xprintf("sdmmc: SEND_SCR failed with retcode = %d\n", st);
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
            if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_APP_CMD, sdmmc->rca << 16, setup_txn)) != MX_OK) {
                xprintf("sdmmc: failed to send app cmd, retcode = %d\n", st);
                break;
            }
            if ((st = sdmmc_do_command(sdmmc->host_mxdev, SDMMC_SET_BUS_WIDTH, 2, setup_txn)) != MX_OK) {
                xprintf("sdmmc: failed to set card bus width, retcode = %d\n", st);
                break;
            }
            const uint32_t new_bus_width = SDMMC_BUS_WIDTH_4;
            // FIXME(yky) use #define
            st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_WIDTH, &new_bus_width,
                              sizeof(new_bus_width), NULL, 0, NULL);
            if (st != MX_OK) {
                xprintf("sdmmc: failed to set host bus width, retcode = %d\n", st);
            }
        } while (false);
    }

err:
    return st;
}

