// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standard Includes
#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/protocol/sdmmc.h>
#include <ddk/debug.h>

#include <pretty/hexdump.h>

#include "sdmmc.h"

#define RCA_ARG(dev) ((dev)->rca << 16)

// SD/MMC shared ops

zx_status_t sdmmc_go_idle(sdmmc_device_t* dev) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_GO_IDLE_STATE,
        .arg = 0,
        .cmd_flags = SDMMC_GO_IDLE_STATE_FLAGS,
    };
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sdmmc_send_status(sdmmc_device_t* dev, uint32_t* response) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_SEND_STATUS,
        .arg = RCA_ARG(dev),
        .cmd_flags = SDMMC_SEND_STATUS_FLAGS,
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st == ZX_OK) {
        *response = req.response[0];
    }
    return st;
}

zx_status_t sdmmc_stop_transmission(sdmmc_device_t* dev) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_STOP_TRANSMISSION,
        .arg = 0,
        .cmd_flags = SDMMC_STOP_TRANSMISSION_FLAGS,
    };
    return sdmmc_request(&dev->host, &req);
}

// SD ops

zx_status_t sd_send_if_cond(sdmmc_device_t* dev) {
    // TODO what is this parameter?
    uint32_t arg = 0x1aa;
    sdmmc_req_t req = {
        .cmd_idx = SD_SEND_IF_COND,
        .arg = arg,
        .cmd_flags = SD_SEND_IF_COND_FLAGS,
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_SEND_IF_COND failed, retcode = %d\n", st);
        return st;
    }
    if ((req.response[0] & 0xfff) != arg) {
        // The card should have replied with the pattern that we sent.
        zxlogf(TRACE, "sd: SDMMC_SEND_IF_COND got bad reply = %"PRIu32"\n",
               req.response[0]);
        return ZX_ERR_BAD_STATE;
    } else {
        return ZX_OK;
    }
}

// MMC ops

zx_status_t mmc_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr) {
    // Request sector addressing if not probing
    uint32_t arg = (ocr == 0) ? ocr : ((1 << 30) | ocr);
    sdmmc_req_t req = {
        .cmd_idx = MMC_SEND_OP_COND,
        .arg = arg,
        .cmd_flags = MMC_SEND_OP_COND_FLAGS,
    };
    zx_status_t st;
    for (int i = 100; i; i--) {
        if ((st = sdmmc_request(&dev->host, &req)) != ZX_OK) {
            // fail on request error
            break;
        }
        // No need to wait for busy clear if probing
        if ((arg == 0) || (req.response[0] & MMC_OCR_BUSY)) {
            *rocr = req.response[0];
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
    return st;
}

zx_status_t mmc_all_send_cid(sdmmc_device_t* dev, uint32_t cid[4]) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_ALL_SEND_CID,
        .arg = 0,
        .cmd_flags = SDMMC_ALL_SEND_CID_FLAGS,
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st == ZX_OK) {
        cid[0] = req.response[0];
        cid[1] = req.response[1];
        cid[2] = req.response[2];
        cid[3] = req.response[3];
    }
    return st;
}

zx_status_t mmc_set_relative_addr(sdmmc_device_t* dev, uint16_t rca) {
    sdmmc_req_t req = {
        .cmd_idx = MMC_SET_RELATIVE_ADDR,
        .arg = (rca << 16),
        .cmd_flags = MMC_SET_RELATIVE_ADDR_FLAGS,
    };
    return sdmmc_request(&dev->host, &req);
}

zx_status_t mmc_send_csd(sdmmc_device_t* dev, uint32_t csd[4]) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_SEND_CSD,
        .arg = RCA_ARG(dev),
        .cmd_flags = SDMMC_SEND_CSD_FLAGS,
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st == ZX_OK) {
        csd[0] = req.response[0];
        csd[1] = req.response[1];
        csd[2] = req.response[2];
        csd[3] = req.response[3];
    }
    return st;
}

zx_status_t mmc_send_ext_csd(sdmmc_device_t* dev, uint8_t ext_csd[512]) {
    // EXT_CSD is send in a data stage
    sdmmc_req_t req = {
        .cmd_idx = MMC_SEND_EXT_CSD,
        .arg = 0,
        .blockcount = 1,
        .blocksize = 512,
        .use_dma = false,
        .virt = ext_csd,
        .cmd_flags = MMC_SEND_EXT_CSD_FLAGS,
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if ((st == ZX_OK) && (driver_get_log_flags() & DDK_LOG_SPEW)) {
        zxlogf(SPEW, "EXT_CSD:\n");
        hexdump8_ex(ext_csd, 512, 0);
    }
    return st;
}

zx_status_t mmc_select_card(sdmmc_device_t* dev) {
    sdmmc_req_t req = {
        .cmd_idx = MMC_SELECT_CARD,
        .arg = RCA_ARG(dev),
        .cmd_flags = MMC_SELECT_CARD_FLAGS,
    };
    return sdmmc_request(&dev->host, &req);
}

zx_status_t mmc_switch(sdmmc_device_t* dev, uint8_t index, uint8_t value) {
    // Send the MMC_SWITCH command
    uint32_t arg = (3 << 24) |  // write byte
                   (index << 16) | (value << 8);
    sdmmc_req_t req = {
        .cmd_idx = MMC_SWITCH,
        .arg = arg,
        .cmd_flags = MMC_SWITCH_FLAGS,
    };
    return sdmmc_request(&dev->host, &req);
}
