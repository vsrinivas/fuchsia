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

#define ACMD41_FLAG_SDHC_SDXC_SUPPORT  0x40000000
#define ACMD41_FLAG_1V8_SWITCH_REQUEST 0x01000000
#define ACMD41_FLAG_VOLTAGE_WINDOW_ALL 0x00ff8000

// The "STRUCTURE" field of the "Card Specific Data" register defines the
// version of the structure and how to interpret the rest of the bits.
#define CSD_STRUCT_V1 0x0
#define CSD_STRUCT_V2 0x1

zx_status_t sdmmc_probe_sd(sdmmc_device_t* dev) {
    dev->rca = 0;  // APP_CMD requires the initial RCA to be zero.

    // Issue the SEND_IF_COND command, this will tell us that we can talk to
    // the card correctly and it will also tell us if the voltage range that we
    // have supplied has been accepted.
    zx_status_t st = sd_send_if_cond(dev);
    if (st != ZX_OK) {
        return st;
    }

    // Get the operating conditions from the card.
    uint32_t ocr;
    if ((st = sd_send_op_cond(dev, 0, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "sd: SDMMC_SD_SEND_OP_COND failed, retcode = %d\n", st);
        return st;
    }

    int attempt = 0;
    const int max_attempts = 10;
    bool card_supports_18v_signalling = false;
    while (true) {
        const uint32_t flags = ACMD41_FLAG_SDHC_SDXC_SUPPORT | ACMD41_FLAG_VOLTAGE_WINDOW_ALL;
        uint32_t ocr;
        if ((st = sd_send_op_cond(dev, flags, &ocr)) != ZX_OK) {
            zxlogf(ERROR, "sd: SD_SEND_OP_COND failed with retcode = %d\n", st);
            return st;
        }

        if (ocr & (1 << 31)) {
            if (!(ocr & OCR_SDHC)) {
                // Card is not an SDHC card. We currently don't support this.
                zxlogf(ERROR, "sd: unsupported card type, must use sdhc card\n");
                return ZX_ERR_NOT_SUPPORTED;
            }
            card_supports_18v_signalling = !!((ocr >> 24) & 0x1);
            break;
        }

        if (++attempt == max_attempts) {
            zxlogf(ERROR, "sd: too many attempt trying to negotiate card OCR\n");
            return ZX_ERR_TIMED_OUT;
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    }

    st = sdmmc_set_bus_freq(&dev->host, 25000000);
    if (st != ZX_OK) {
        // This is non-fatal but the card will run slowly.
        zxlogf(ERROR, "sd: failed to increase bus frequency.\n");
    }

    // TODO(bradenkell): Re-enable support for UHS-I mode once the Mediatek driver supports
    //                   switching to 1.8V.

    (void)card_supports_18v_signalling;
    // Try to switch the bus voltage to 1.8v
    // if (card_supports_18v_signalling) {
    //     st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_VOLTAGE_SWITCH, 0, setup_txn);
    //     if (st != ZX_OK) {
    //         zxlogf(ERROR, "sd: failed to send switch voltage command to card, "
    //                 "retcode = %d\n", st);
    //         goto err;
    //     }
    //
    //     st = sdmmc_set_signal_voltage(&sdmmc->host, SDMMC_VOLTAGE_180);
    //     if (st != ZX_OK) {
    //         zxlogf(ERROR, "sd: Card supports 1.8v signalling but was unable to "
    //                 "switch to 1.8v mode, retcode = %d\n", st);
    //         goto err;
    //     }
    // }

    if ((st = mmc_all_send_cid(dev, dev->raw_cid)) != ZX_OK) {
        zxlogf(ERROR, "sd: ALL_SEND_CID failed with retcode = %d\n", st);
        return st;
    }

    uint16_t card_status;
    if ((st = sd_send_relative_addr(dev, &dev->rca, &card_status)) != ZX_OK) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with retcode = %d\n", st);
        return st;
    }

    dev->type = SDMMC_TYPE_SD;
    if (card_status & 0xe000) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with resp = %d\n",
                (card_status & 0xe000));
        return ZX_ERR_INTERNAL;
    }
    if ((card_status & (1u << 8)) == 0) {
        zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed. Card not ready.\n");
        return ZX_ERR_INTERNAL;
    }

    // Determine the size of the card.
    if ((st = mmc_send_csd(dev, dev->raw_csd)) != ZX_OK) {
        zxlogf(ERROR, "sd: failed to send app cmd, retcode = %d\n", st);
        return st;
    }

    // For now we only support SDHC cards. These cards must have a CSD type = 1,
    // since CSD type 0 is unable to support SDHC sized cards.
    uint8_t csd_structure = (dev->raw_csd[3] >> 30) & 0x3;
    if (csd_structure != CSD_STRUCT_V2) {
        zxlogf(ERROR, "sd: unsupported card type, expected CSD version = %d, "
                "got version %d\n", CSD_STRUCT_V2, csd_structure);
        return ZX_ERR_INTERNAL;
    }

    const uint32_t c_size = ((dev->raw_csd[1] >> 16) |
                             (dev->raw_csd[2] << 16)) & 0x3fffff;
    dev->block_info.block_count = (c_size + 1ul) * 1024ul;
    dev->block_info.block_size = 512ul;
    dev->capacity = dev->block_info.block_size * dev->block_info.block_count;
    zxlogf(INFO, "sd: found card with capacity = %"PRIu64"B\n", dev->capacity);

    if ((st = sd_select_card(dev)) != ZX_OK) {
        zxlogf(ERROR, "sd: SELECT_CARD failed with retcode = %d\n", st);
        return st;
    }

    uint8_t scr[8];
    if ((st = sd_send_scr(dev, scr)) != ZX_OK) {
        zxlogf(ERROR, "sd: SEND_SCR failed with retcode = %d\n", st);
        return st;
    }

    // If this card supports 4 bit mode, then put it into 4 bit mode.
    const uint32_t supported_bus_widths = scr[1] & 0xf;
    if (supported_bus_widths & 0x4) {
        do {
            // First tell the card to go into four bit mode:
            if ((st = sd_set_bus_width(dev, SDMMC_BUS_WIDTH_FOUR)) != ZX_OK) {
                zxlogf(ERROR, "sd: failed to set card bus width, retcode = %d\n", st);
                break;
            }
            st = sdmmc_set_bus_width(&dev->host, SDMMC_BUS_WIDTH_FOUR);
            if (st != ZX_OK) {
                zxlogf(ERROR, "sd: failed to set host bus width, retcode = %d\n", st);
            }
        } while (false);
    }

    return ZX_OK;
}

