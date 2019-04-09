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

static inline uint32_t get_bits(uint32_t x, uint32_t mask, uint32_t loc) {
    return (x & mask) >> loc;
}

static inline void update_bits(uint32_t *x, uint32_t mask, uint32_t loc, uint32_t val) {
    *x &= ~mask;
    *x |= ((val << loc) & mask);
}

zx_status_t sdmmc_request_helper(sdmmc_device_t* dev, sdmmc_req_t* req,
                                 uint8_t retries, uint32_t wait_time) {
    zx_status_t st;
    while (((st = sdmmc_request(&dev->host, req)) != ZX_OK) && retries > 0) {
        retries--;
        zx_nanosleep(zx_deadline_after(ZX_MSEC(wait_time)));
    }
    return st;
}

// SD/MMC shared ops

zx_status_t sdmmc_go_idle(sdmmc_device_t* dev) {
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_GO_IDLE_STATE;
    req.arg = 0;
    req.cmd_flags = SDMMC_GO_IDLE_STATE_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sdmmc_send_status(sdmmc_device_t* dev, uint32_t* response) {
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_SEND_STATUS;
    req.arg = RCA_ARG(dev);
    req.cmd_flags = SDMMC_SEND_STATUS_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st == ZX_OK) {
        *response = req.response[0];
    }
    return st;
}

zx_status_t sdmmc_stop_transmission(sdmmc_device_t* dev) {
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_STOP_TRANSMISSION;
    req.arg = 0;
    req.cmd_flags = SDMMC_STOP_TRANSMISSION_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

// SD ops

static zx_status_t sd_send_app_cmd(sdmmc_device_t* dev) {
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_APP_CMD;
    req.arg = RCA_ARG(dev);
    req.cmd_flags = SDMMC_APP_CMD_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sd_send_op_cond(sdmmc_device_t* dev, uint32_t flags, uint32_t* ocr) {
    zx_status_t st = sd_send_app_cmd(dev);
    if (st != ZX_OK) {
        return st;
    }

    sdmmc_req_t req = {};
    req.cmd_idx = SD_APP_SEND_OP_COND;
    req.arg = flags;
    req.cmd_flags = SD_APP_SEND_OP_COND_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    if ((st = sdmmc_request(&dev->host, &req)) != ZX_OK) {
        return st;
    }

    *ocr = req.response[0];
    return ZX_OK;
}

zx_status_t sd_send_if_cond(sdmmc_device_t* dev) {
    // TODO what is this parameter?
    uint32_t arg = 0x1aa;
    sdmmc_req_t req = {};
    req.cmd_idx = SD_SEND_IF_COND;
    req.arg = arg;
    req.cmd_flags = SD_SEND_IF_COND_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_SEND_IF_COND failed, retcode = %d\n", st);
        return st;
    }
    if ((req.response[0] & 0xfff) != arg) {
        // The card should have replied with the pattern that we sent.
        zxlogf(TRACE, "sd: SDMMC_SEND_IF_COND got bad reply = %" PRIu32 "\n",
               req.response[0]);
        return ZX_ERR_BAD_STATE;
    } else {
        return ZX_OK;
    }
}

zx_status_t sd_send_relative_addr(sdmmc_device_t* dev, uint16_t* rca, uint16_t* card_status) {
    sdmmc_req_t req = {};
    req.cmd_idx = SD_SEND_RELATIVE_ADDR;
    req.arg = 0;
    req.cmd_flags = SD_SEND_RELATIVE_ADDR_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);

    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_SEND_RELATIVE_ADDR failed, retcode = %d\n", st);
        return st;
    }

    if (rca != NULL) {
        *rca = static_cast<uint16_t>((req.response[0]) >> 16);
    }
    if (card_status != NULL) {
        *card_status = req.response[0] & 0xffff;
    }

    return st;
}

zx_status_t sd_select_card(sdmmc_device_t* dev) {
    sdmmc_req_t req = {};
    req.cmd_idx = SD_SELECT_CARD;
    req.arg = RCA_ARG(dev);
    req.cmd_flags = SD_SELECT_CARD_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sd_send_scr(sdmmc_device_t* dev, uint8_t scr[8]) {
    zx_status_t st = sd_send_app_cmd(dev);
    if (st != ZX_OK) {
        return st;
    }

    sdmmc_req_t req = {};
    req.cmd_idx = SD_APP_SEND_SCR;
    req.arg = 0;
    req.cmd_flags = SD_APP_SEND_SCR_FLAGS;
    req.blockcount = 1;
    req.blocksize = 8;
    req.use_dma = false;
    req.virt_buffer = scr;
    req.virt_size = 8;
    req.buf_offset = 0;
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sd_set_bus_width(sdmmc_device_t* dev, sdmmc_bus_width_t width) {
    if (width != SDMMC_BUS_WIDTH_ONE && width != SDMMC_BUS_WIDTH_FOUR) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t st = sd_send_app_cmd(dev);
    if (st != ZX_OK) {
        return st;
    }

    sdmmc_req_t req = {};
    req.cmd_idx = SD_APP_SET_BUS_WIDTH;
    req.arg = (width == SDMMC_BUS_WIDTH_FOUR ? 2 : 0);
    req.cmd_flags = SD_APP_SET_BUS_WIDTH_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t sd_switch_uhs_voltage(sdmmc_device_t *dev, uint32_t ocr) {
    zx_status_t st = ZX_OK;
    sdmmc_req_t req = {};
    req.cmd_idx = SD_VOLTAGE_SWITCH;
    req.arg = ocr;
    req.cmd_flags = SD_VOLTAGE_SWITCH_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);

    if (dev->signal_voltage == SDMMC_VOLTAGE_V180) {
        return ZX_OK;
    }

    st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_VOLTAGE_SWITCH failed, retcode = %d\n", st);
        return st;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));
    //TODO: clock gating while switching voltage
    st = sdmmc_set_signal_voltage(&dev->host, SDMMC_VOLTAGE_V180);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sd: SD_VOLTAGE_SWITCH failed, retcode = %d\n", st);
        return st;
    }
    return ZX_OK;
}

// SDIO specific ops

zx_status_t sdio_send_op_cond(sdmmc_device_t* dev, uint32_t ocr, uint32_t* rocr) {
    zx_status_t st = ZX_OK;
    sdmmc_req_t req = {};
    req.cmd_idx = SDIO_SEND_OP_COND;
    req.arg = ocr;
    req.cmd_flags = SDIO_SEND_OP_COND_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    req.probe_tuning_cmd = true;
    for (size_t i = 0; i < 100; i++) {
        if ((st = sdmmc_request_helper(dev, &req, 3, 10)) != ZX_OK) {
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
    sdmmc_req_t req = {};
    req.cmd_idx = SDIO_IO_RW_DIRECT;
    req.arg = cmd_arg;
    req.cmd_flags = SDIO_IO_RW_DIRECT_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    if (reg_addr == SDIO_CIA_CCCR_ASx_ABORT_SEL_CR_ADDR) {
        req.probe_tuning_cmd = true;
    }
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if (st != ZX_OK && reg_addr == SDIO_CIA_CCCR_ASx_ABORT_SEL_CR_ADDR) {
        // Do not log error if ABORT fails during reset, as it proved to be harmless.
        // TODO(ravoorir): Is it expected for the command to fail intermittently during reset?
        zxlogf(TRACE, "sdio: SDIO_IO_RW_DIRECT failed, retcode = %d\n", st);
        return st;
    } else if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: SDIO_IO_RW_DIRECT failed, retcode = %d\n", st);
        return st;
    }
    if (read_byte) {
        *read_byte = static_cast<uint8_t>(get_bits(req.response[0],
                                                   SDIO_IO_RW_DIRECT_RESP_READ_BYTE_MASK,
                                                   SDIO_IO_RW_DIRECT_RESP_READ_BYTE_LOC));
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
        if (dev->sdio_dev.hw_info.caps & SDIO_CARD_MULTI_BLOCK) {
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
    sdmmc_req_t req = {};
    req.cmd_idx = SDIO_IO_RW_DIRECT_EXTENDED;
    req.arg = cmd_arg;
    req.cmd_flags = write ? (SDIO_IO_RW_DIRECT_EXTENDED_FLAGS) :
                (SDIO_IO_RW_DIRECT_EXTENDED_FLAGS | SDMMC_CMD_READ),
    req.blockcount = static_cast<uint16_t>(blk_count);
    req.blocksize = static_cast<uint16_t>(blk_size);

    if (use_dma) {
        req.virt_buffer = NULL;
        req.dma_vmo = dma_vmo;
        req.buf_offset = buf_offset;
    } else {
        req.virt_buffer = buf + buf_offset;
        req.virt_size = blk_size;
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
    sdmmc_req_t req = {};
    req.cmd_idx = MMC_SEND_OP_COND;
    req.arg = arg;
    req.cmd_flags = MMC_SEND_OP_COND_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
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
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_ALL_SEND_CID;
    req.arg = 0;
    req.cmd_flags = SDMMC_ALL_SEND_CID_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
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
    sdmmc_req_t req = {};
    req.cmd_idx = MMC_SET_RELATIVE_ADDR;
    req.arg = (rca << 16);
    req.cmd_flags = MMC_SET_RELATIVE_ADDR_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t mmc_send_csd(sdmmc_device_t* dev, uint32_t csd[4]) {
    sdmmc_req_t req = {};
    req.cmd_idx = SDMMC_SEND_CSD;
    req.arg = RCA_ARG(dev);
    req.cmd_flags = SDMMC_SEND_CSD_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
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
    sdmmc_req_t req = {};
    req.cmd_idx = MMC_SEND_EXT_CSD;
    req.arg = 0;
    req.blockcount = 1;
    req.blocksize = 512;
    req.use_dma = false;
    req.virt_buffer = ext_csd;
    req.virt_size = 512;
    req.cmd_flags = MMC_SEND_EXT_CSD_FLAGS;
    zx_status_t st = sdmmc_request(&dev->host, &req);
    if ((st == ZX_OK) && (driver_get_log_flags() & DDK_LOG_SPEW)) {
        zxlogf(SPEW, "EXT_CSD:\n");
        hexdump8_ex(ext_csd, 512, 0);
    }
    return st;
}

zx_status_t mmc_select_card(sdmmc_device_t* dev) {
    sdmmc_req_t req = {};
    req.cmd_idx = MMC_SELECT_CARD;
    req.arg = RCA_ARG(dev);
    req.cmd_flags = MMC_SELECT_CARD_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}

zx_status_t mmc_switch(sdmmc_device_t* dev, uint8_t index, uint8_t value) {
    // Send the MMC_SWITCH command
    uint32_t arg = (3 << 24) |  // write byte
                   (index << 16) | (value << 8);
    sdmmc_req_t req = {};
    req.cmd_idx = MMC_SWITCH;
    req.arg = arg;
    req.cmd_flags = MMC_SWITCH_FLAGS;
    req.use_dma = sdmmc_use_dma(dev);
    return sdmmc_request(&dev->host, &req);
}
