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
#include <hw/sdio.h>

#include <pretty/hexdump.h>

#include "sdmmc.h"

#define RCA_ARG(dev) ((dev)->rca << 16)

// SD/MMC shared ops

zx_status_t sdmmc_go_idle(sdmmc_device_t* dev) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_GO_IDLE_STATE,
        .arg = 0,
        .cmd_flags = SDMMC_GO_IDLE_STATE_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
    };
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sdmmc_send_status(sdmmc_device_t* dev, uint32_t* response) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_SEND_STATUS,
        .arg = RCA_ARG(dev),
        .cmd_flags = SDMMC_SEND_STATUS_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
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

zx_status_t sd_send_relative_addr(sdmmc_device_t* dev, uint16_t *rca) {
    sdmmc_req_t req = {
        .cmd_idx = SD_SEND_RELATIVE_ADDR,
        .arg = 0,
        .cmd_flags = SD_SEND_RELATIVE_ADDR_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
    };

    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_SEND_RELATIVE_ADDR failed, retcode = %d\n", st);
        return st;
    }

    if (rca != NULL) {
        *rca = (req.response[0]) >> 16;
    }
    return st;
}

zx_status_t sd_switch_uhs_voltage(sdmmc_device_t *dev, uint32_t ocr) {
    zx_status_t st = ZX_OK;
    sdmmc_req_t req = {
        .cmd_idx = SD_VOLTAGE_SWITCH,
        .arg = ocr,
        .cmd_flags = SD_VOLTAGE_SWITCH_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
    };

    if (dev->signal_voltage == SDMMC_VOLTAGE_180) {
        return ZX_OK;
    }

    st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_VOLTAGE_SWITCH failed, retcode = %d\n", st);
        return st;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));
    //TODO: clock gating while switching voltage
    st = sdmmc_set_signal_voltage(&dev->host, SDMMC_VOLTAGE_180);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_VOLTAGE_SWITCH failed, retcode = %d\n", st);
        return st;
    }
    return ZX_OK;
}

// SDIO specific ops

zx_status_t sdio_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr) {
    zx_status_t st = ZX_OK;
    sdmmc_req_t req = {
        .cmd_idx = SDIO_SEND_OP_COND,
        .arg = ocr,
        .cmd_flags = SDIO_SEND_OP_COND_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
    };
    for (size_t i = 0; i < 100; i++) {
        if ((st = sdmmc_request(&dev->host, &req)) != ZX_OK) {
            // fail on request error
            break;
        }
        // No need to wait for busy clear if probing
        if ((ocr == 0) || (req.response[0] & MMC_OCR_BUSY)) {
            *rocr = req.response[0];
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
    return st;
}

zx_status_t sdio_io_rw_direct(sdmmc_device_t* dev, bool write, uint32_t fn_idx,
                              uint32_t reg_addr, uint8_t write_byte, uint8_t *read_byte) {
    uint32_t cmd_arg = 0;
    if (write) {
        cmd_arg |= SDIO_IO_RW_DIRECT_RW_FLAG;
        if (read_byte) {
            cmd_arg |= SDIO_IO_RW_DIRECT_RAW_FLAG;
        }
    }
    update_bits(&cmd_arg, SDIO_IO_RW_DIRECT_FN_IDX_MASK, SDIO_IO_RW_DIRECT_FN_IDX_LOC,
                fn_idx);
    update_bits(&cmd_arg, SDIO_IO_RW_DIRECT_REG_ADDR_MASK, SDIO_IO_RW_DIRECT_REG_ADDR_LOC,
                reg_addr);
    update_bits(&cmd_arg, SDIO_IO_RW_DIRECT_WRITE_BYTE_MASK, SDIO_IO_RW_DIRECT_WRITE_BYTE_LOC,
                write_byte);
    sdmmc_req_t req = {
        .cmd_idx = SDIO_IO_RW_DIRECT,
        .arg = cmd_arg,
        .cmd_flags = SDIO_IO_RW_DIRECT_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
    };
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: SDIO_IO_RW_DIRECT failed, retcode = %d\n", st);
        return st;
    }
    if (read_byte) {
        *read_byte = get_bits(req.response[0], SDIO_IO_RW_DIRECT_RESP_READ_BYTE_MASK,
                              SDIO_IO_RW_DIRECT_RESP_READ_BYTE_LOC);
    }
    return ZX_OK;
}

zx_status_t sdio_io_rw_extended(sdmmc_device_t *dev, bool write, uint32_t fn_idx,
                                uint32_t reg_addr, bool incr, uint32_t blk_count,
                                uint32_t blk_size,  bool use_dma, uint8_t *buf,
                                zx_handle_t dma_vmo, uint64_t buf_offset) {

    uint32_t cmd_arg = 0;
    if (write) {
        cmd_arg |= SDIO_IO_RW_EXTD_RW_FLAG;
    }
    update_bits(&cmd_arg, SDIO_IO_RW_EXTD_FN_IDX_MASK, SDIO_IO_RW_EXTD_FN_IDX_LOC,
                fn_idx);
    update_bits(&cmd_arg, SDIO_IO_RW_EXTD_REG_ADDR_MASK, SDIO_IO_RW_EXTD_REG_ADDR_LOC,
                reg_addr);
    if (incr) {
        cmd_arg |= SDIO_IO_RW_EXTD_OP_CODE_INCR;
    }

    if (blk_count > 1) {
        if (dev->sdio_info.caps & SDIO_CARD_MULTI_BLOCK) {
            cmd_arg |= SDIO_IO_RW_EXTD_BLOCK_MODE;
            update_bits(&cmd_arg, SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_MASK,
                        SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_LOC, blk_count);
        } else {
            //Convert the request into byte mode?
            return ZX_ERR_NOT_SUPPORTED;
        }
    } else {
        //SDIO Spec Table 5-3
        uint32_t arg_blk_size = (blk_size == 512) ? 0 : blk_size;
        update_bits(&cmd_arg, SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_MASK,
                    SDIO_IO_RW_EXTD_BYTE_BLK_COUNT_LOC, arg_blk_size);
    }
    sdmmc_req_t req = {
        .cmd_idx = SDIO_IO_RW_DIRECT_EXTENDED,
        .arg = cmd_arg,
        .cmd_flags = write ? (SDIO_IO_RW_DIRECT_EXTENDED_FLAGS) :
                    (SDIO_IO_RW_DIRECT_EXTENDED_FLAGS | SDMMC_CMD_READ),
        .blockcount = blk_count,
        .blocksize = blk_size,
    };

    if (use_dma) {
        req.virt = NULL;
        req.dma_vmo = dma_vmo;
        req.buf_offset = buf_offset;
    } else {
        req.virt = buf + buf_offset;
    }
    req.use_dma = use_dma;

    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: SDIO_IO_RW_DIRECT_EXTENDED failed, retcode = %d\n", st);
        return st;
    }
    return ZX_OK;
}

// MMC ops

zx_status_t mmc_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr) {
    // Request sector addressing if not probing
    uint32_t arg = (ocr == 0) ? ocr : ((1 << 30) | ocr);
    sdmmc_req_t req = {
        .cmd_idx = MMC_SEND_OP_COND,
        .arg = arg,
        .cmd_flags = MMC_SEND_OP_COND_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
    };
    return sdmmc_request(&dev->host, &req);
}

zx_status_t mmc_send_csd(sdmmc_device_t* dev, uint32_t csd[4]) {
    sdmmc_req_t req = {
        .cmd_idx = SDMMC_SEND_CSD,
        .arg = RCA_ARG(dev),
        .cmd_flags = SDMMC_SEND_CSD_FLAGS,
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
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
        .use_dma = sdmmc_use_dma(dev),
    };
    return sdmmc_request(&dev->host, &req);
}
