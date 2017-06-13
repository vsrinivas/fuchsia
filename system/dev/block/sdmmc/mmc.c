// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/protocol/sdmmc.h>

#include <pretty/hexdump.h>

#include "sdmmc.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

static mx_status_t mmc_send_op_cond(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t ocr, uint32_t* rocr) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    // Request sector addressing if not probing
    uint32_t arg = (ocr == 0) ? ocr : ((1 << 30) | ocr);
    for (int i = 100; i; i--) {
        if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_OP_COND, arg, txn)) != MX_OK) {
            // fail on txn error
            break;
        }
        // No need to wait for busy clear if probing
        if ((arg == 0) || (pdata->response[0] & (1 << 31))) {
            *rocr = pdata->response[0];
            break;
        }
        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }
    return st;
}

static mx_status_t mmc_all_send_cid(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t cid[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_ALL_SEND_CID, 0, txn)) == MX_OK) {
        cid[0] = pdata->response[0];
        cid[1] = pdata->response[1];
        cid[2] = pdata->response[2];
        cid[3] = pdata->response[3];
    }
    return st;
}

static mx_status_t mmc_set_relative_addr(sdmmc_t* sdmmc, iotxn_t* txn, uint16_t rca) {
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SET_RELATIVE_ADDR, (rca << 16), txn);
}

static mx_status_t mmc_send_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint32_t csd[4]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_CSD, sdmmc->rca << 16, txn)) == MX_OK) {
        csd[0] = pdata->response[0];
        csd[1] = pdata->response[1];
        csd[2] = pdata->response[2];
        csd[3] = pdata->response[3];
    }
    return st;
}

static mx_status_t mmc_send_ext_csd(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t ext_csd[512]) {
    sdmmc_protocol_data_t* pdata = iotxn_pdata(txn, sdmmc_protocol_data_t);
    mx_status_t st;
    // EXT_CSD is send in a data stage
    pdata->blockcount = 1;
    pdata->blocksize = 512;
    if ((st = sdmmc_do_command(sdmmc->host_mxdev, MMC_SEND_EXT_CSD, 0, txn)) == MX_OK) {
        iotxn_copyfrom(txn, ext_csd, 512, 0);
    }
    return st;
}

static mx_status_t mmc_select_card(sdmmc_t* sdmmc, iotxn_t* txn) {
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SELECT_CARD, sdmmc->rca << 16, txn);
}

static mx_status_t mmc_switch(sdmmc_t* sdmmc, iotxn_t* txn, uint8_t index, uint8_t value) {
    uint32_t arg = (3 << 24) |  // write byte
                   (index << 16) | (value << 8);
    return sdmmc_do_command(sdmmc->host_mxdev, MMC_SWITCH, arg, txn);
}

static mx_status_t mmc_decode_cid(sdmmc_t* sdmmc, const uint8_t* raw_cid) {
    printf("sdmmc: product name=%c%c%c%c%c%c\n",
            raw_cid[6], raw_cid[7], raw_cid[8], raw_cid[9], raw_cid[10], raw_cid[11]);
    printf("       revision=%u.%u\n", (raw_cid[5] >> 4) & 0xf, raw_cid[5] & 0xf);
    printf("       serial=%u\n", *((uint32_t*)&raw_cid[1]));
    return MX_OK;
}

static mx_status_t mmc_decode_csd(sdmmc_t* sdmmc, const uint8_t* raw_csd) {
    uint8_t spec_vrsn = (raw_csd[14] >> 2) & 0xf;
    // Only support spec version > 4.0
    if (spec_vrsn < MMC_CID_SPEC_VRSN_40) {
        return MX_ERR_NOT_SUPPORTED;
    }

    xprintf("sdmmc: CSD version %u spec version %u\n", (raw_csd[14] >> 6) & 0x3, spec_vrsn);

    // Only support high capacity (> 2GB) cards
    uint16_t c_size = (raw_csd[6] >> 2 & 0x3f) |
                      ((raw_csd[7] & 0x3f) << 6);
    if (c_size != 0xfff) {
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
}

static mx_status_t mmc_decode_ext_csd(sdmmc_t* sdmmc, const uint8_t* raw_ext_csd) {
    xprintf("sdmmc: EXT_CSD version %u CSD version %u\n", raw_ext_csd[192], raw_ext_csd[194]);

    // Get the capacity for the card
    uint32_t sectors = (raw_ext_csd[212] << 0) | (raw_ext_csd[213] << 8) | (raw_ext_csd[214] << 16) | (raw_ext_csd[215] << 24);
    sdmmc->capacity = sectors * 512ul;

    printf("sdmmc: found card with capacity = %" PRIu64 "B\n", sdmmc->capacity);

    return MX_OK;
}

mx_status_t sdmmc_probe_mmc(sdmmc_t* sdmmc, iotxn_t* setup_txn) {
    mx_status_t st;

    // Query OCR
    uint32_t ocr = 0;
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Check if the card matches the host's supported voltages and indicate sector mode
    // TODO check with host
    if ((st = mmc_send_op_cond(sdmmc, setup_txn, ocr, &ocr)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_OP_COND failed, retcode = %d\n", st);
        goto err;
    }

    // Get CID from card
    // Only 1 card on eve so no need to loop
    if ((st = mmc_all_send_cid(sdmmc, setup_txn, sdmmc->raw_cid)) != MX_OK) {
        xprintf("sdmmc: MMC_ALL_SEND_CID failed, retcode = %d\n", st);
        goto err;
    }
    xprintf("sdmmc: MMC_ALL_SEND_CID cid 0x%08x 0x%08x 0x%08x 0x%08x\n",
        sdmmc->raw_cid[0],
        sdmmc->raw_cid[1],
        sdmmc->raw_cid[2],
        sdmmc->raw_cid[3]);

    mmc_decode_cid(sdmmc, (const uint8_t*)sdmmc->raw_cid);

    sdmmc->type = SDMMC_TYPE_MMC;
    sdmmc->rca = 1;

    // Set relative card address
    if ((st = mmc_set_relative_addr(sdmmc, setup_txn, sdmmc->rca)) != MX_OK) {
        xprintf("sdmmc: MMC_SET_RELATIVE_ADDR failed, retcode = %d\n", st);
        goto err;
    }

    // Read CSD register
    if ((st = mmc_send_csd(sdmmc, setup_txn, sdmmc->raw_csd)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_csd(sdmmc, (const uint8_t*)sdmmc->raw_csd)) != MX_OK) {
        goto err;
    }

    // Select the card
    if ((st = mmc_select_card(sdmmc, setup_txn)) != MX_OK) {
        xprintf("sdmmc: MMC_SELECT_CARD failed, retcode = %d\n", st);
        goto err;
    }

    // Read extended CSD register
    if ((st = mmc_send_ext_csd(sdmmc, setup_txn, sdmmc->raw_ext_csd)) != MX_OK) {
        xprintf("sdmmc: MMC_SEND_EXT_CSD failed, retcode = %d\n", st);
        goto err;
    }

    if ((st = mmc_decode_ext_csd(sdmmc, (const uint8_t*)sdmmc->raw_ext_csd)) != MX_OK) {
        goto err;
    }

    // Set the bus frequency to legacy timing
    // TODO switch to HS200/HS400 timing
    uint32_t bus_freq = 25000000;
    if ((st = device_ioctl(sdmmc->host_mxdev, IOCTL_SDMMC_SET_BUS_FREQ, &bus_freq, sizeof(bus_freq), NULL, 0, NULL)) != MX_OK) {
        xprintf("sdmmc: failed to set host bus frequency, retcode = %d\n", st);
        goto err;
    }

err:
    return st;
}
