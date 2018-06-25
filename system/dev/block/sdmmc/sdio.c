// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/param.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/protocol/sdio.h>
#include <hw/sdio.h>

#include <zircon/threads.h>

#include "sdmmc.h"
#include "sdio.h"

static zx_status_t sdio_read_byte(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                  uint8_t *read_byte) {
    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }
    return sdio_io_rw_direct(dev, false, fn_idx, addr, 0, read_byte);
}

static zx_status_t sdio_write_byte(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                   uint8_t write_byte) {
    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }
    return sdio_io_rw_direct(dev, true, fn_idx, addr, write_byte, NULL);
}

static zx_status_t sdio_read_after_write_byte(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                              uint8_t write_byte, uint8_t *read_byte) {
    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }
    return sdio_io_rw_direct(dev, true, fn_idx, addr, write_byte, read_byte);
}

zx_status_t sdio_rw_data(void *ctx, uint8_t fn_idx, sdio_rw_txn_t *txn) {
    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdmmc_device_t *dev = ctx;
    zx_status_t st = ZX_OK;
    uint32_t addr = txn->addr;
    void *buf = txn->buf;
    uint32_t data_size = txn->data_size;
    uint32_t func_blk_size = (dev->sdio_info.funcs[fn_idx]).cur_blk_size;
    uint32_t data_processed = 0;
    uint32_t rem_blocks = (func_blk_size == 0) ? 0 : (data_size / func_blk_size);
    bool mbs = (dev->sdio_info.caps) & SDIO_CARD_MULTI_BLOCK;

    while (rem_blocks > 0) {
        uint32_t num_blocks = 1;
        uint32_t max_host_blocks = (dev->host_info.max_transfer_size) / (func_blk_size);
        if (mbs) {
            //multiblock is supported, figure out max number of blocks per cmd
            num_blocks = MIN(MIN(SDIO_IO_RW_EXTD_MAX_BLKS_PER_CMD, max_host_blocks), rem_blocks);
        }
        st = sdio_io_rw_extended(dev, txn->write, fn_idx, addr, txn->incr, buf, num_blocks,
                                 func_blk_size);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdio_rw_data: Error %sing data.func: %d status: %d\n",
                   txn->write ? "writ" : "read", fn_idx, st);
            return st;
        }
        rem_blocks -= num_blocks;
        data_processed += num_blocks * func_blk_size;
        buf += data_processed;
        if (txn->incr) {
            addr += data_processed;
        }
    }

    if (data_processed < data_size) {
        //Write the remaining data.
        st = sdio_io_rw_extended(dev, txn->write, fn_idx, addr, txn->incr,
                                 buf + data_processed, 1, (data_size - data_processed));
    }
    return st;
}

static zx_status_t sdio_read_data(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                  uint32_t data_size, void *buf) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = false;
    txn.buf = buf;
    txn.data_size = data_size;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_write_data(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                   uint32_t data_size, void *buf) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = true;
    txn.buf = buf;
    txn.data_size = data_size;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_read_data32(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                    uint32_t *dword) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = false;
    txn.buf = dword;
    txn.data_size = 4;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_write_data32(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                     uint32_t dword) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = true;
    txn.buf = (void *)&dword;
    txn.data_size = 4;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_read_data16(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                    uint16_t *word) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = false;
    txn.buf = word;
    txn.data_size = 2;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_write_data16(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                     uint16_t word) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = true;
    txn.buf = (void *)&word;
    txn.data_size = 2;
    txn.incr = true;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_read_data_fifo(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                       uint32_t data_size, void *buf) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = false;
    txn.buf = buf;
    txn.data_size = 2;
    txn.incr = false;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_write_data_fifo(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                        uint32_t data_size, void *buf) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = true;
    txn.buf = buf;
    txn.data_size = 2;
    txn.incr = false;
    return sdio_rw_data(dev, fn_idx, &txn);
}

zx_status_t sdio_get_oob_irq_host(void *ctx, zx_handle_t *oob_irq) {
    return sdmmc_get_sdio_oob_irq(ctx, oob_irq);
}

static uint32_t sdio_read_tuple_body(uint8_t *t_body, size_t start, size_t numbytes) {
    uint32_t res = 0;

    for (size_t i = start; i < (start + numbytes); i++) {
        res |= t_body[i] << ((i - start)* 8);
    }
    return res;
}

static zx_status_t sdio_process_cccr(sdmmc_device_t *dev) {
    zx_status_t status = ZX_OK;
    uint8_t cccr_vsn, sdio_vsn, vsn_info, bus_speed, card_caps;
    uint32_t max_blk_sz = dev->sdio_info.funcs[0].max_blk_size;

    if (max_blk_sz >= SDIO_CIA_CCCR_NON_VENDOR_REG_SIZE) {
        uint8_t cccr[SDIO_CIA_CCCR_NON_VENDOR_REG_SIZE] = {0};
        //Read all of CCCR at a time to avoid multiple read commands
        status = sdio_io_rw_extended(dev, false, 0, SDIO_CIA_CCCR_CCCR_SDIO_VER_ADDR, true,
                                     cccr, 1, SDIO_CIA_CCCR_NON_VENDOR_REG_SIZE);
        vsn_info = cccr[SDIO_CIA_CCCR_CCCR_SDIO_VER_ADDR];
        card_caps = cccr[SDIO_CIA_CCCR_CARD_CAPS_ADDR];
        bus_speed = cccr[SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR];
    }

    if (status != ZX_OK || (max_blk_sz < SDIO_CIA_CCCR_NON_VENDOR_REG_SIZE)) {
        status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_CCCR_SDIO_VER_ADDR, 0, &vsn_info);
        if (status != ZX_OK) {
            zxlogf(ERROR, "sdio_process_cccr: Error reading CCCR reg: %d\n", status);
            return status;
        }
        status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_CARD_CAPS_ADDR, 0, &card_caps);
        if (status != ZX_OK) {
            zxlogf(ERROR, "sdio_process_cccr: Error reading CAPS reg: %d\n", status);
            return status;
        }
        status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, 0, &bus_speed);
        if (status != ZX_OK) {
            zxlogf(ERROR, "sdio_process_cccr: Error reading SPEED reg: %d\n", status);
            return status;
        }
    }
    cccr_vsn = get_bits(vsn_info, SDIO_CIA_CCCR_CCCR_VER_MASK, SDIO_CIA_CCCR_CCCR_VER_LOC);
    sdio_vsn = get_bits(vsn_info, SDIO_CIA_CCCR_SDIO_VER_MASK, SDIO_CIA_CCCR_SDIO_VER_LOC);
    if ((cccr_vsn != SDIO_CCCR_FORMAT_VER_3) || (sdio_vsn != SDIO_SDIO_VER_3)) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    dev->sdio_info.cccr_vsn = cccr_vsn;
    dev->sdio_info.sdio_vsn = sdio_vsn;
    dev->sdio_info.caps = 0;
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_SMB) {
        dev->sdio_info.caps |= SDIO_CARD_MULTI_BLOCK;
    }
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_LSC) {
        dev->sdio_info.caps |= SDIO_CARD_LOW_SPEED;
    }
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_4BLS) {
        dev->sdio_info.caps |= SDIO_CARD_4BIT_BUS;
    }
    if (bus_speed & SDIO_CIA_CCCR_BUS_SPEED_SEL_SHS) {
        dev->sdio_info.caps |= SDIO_CARD_HIGH_SPEED;
    }
    return status;
}

static zx_status_t sdio_parse_func_ext_tuple(sdmmc_device_t* dev, uint32_t fn_idx,
                                             sdio_func_tuple_t *tup) {
    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    if (fn_idx == 0) {
        if (tup->t_body_size < SDIO_CIS_TPL_FUNC0_FUNCE_MIN_BDY_SZ) {
            return ZX_ERR_IO;
        }
        func->max_blk_size = sdio_read_tuple_body(tup->t_body,
                                                  SDIO_CIS_TPL_FUNCE_FUNC0_MAX_BLK_SIZE_LOC, 2);
        func->max_blk_size = MIN(dev->host_info.max_transfer_size, func->max_blk_size);
        uint8_t speed_val = get_bits_u8(tup->t_body[3], SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_MASK,
                                        SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_LOC);
        uint8_t speed_unit = get_bits_u8(tup->t_body[3],
                                         SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_MASK,
                                         SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_LOC);
        func->max_tran_speed = sdio_cis_tpl_funce_tran_speed_val[speed_val] *
                               sdio_cis_tpl_funce_tran_speed_unit[speed_unit];
        return ZX_OK;
    }

    if (tup->t_body_size < SDIO_CIS_TPL_FUNCx_FUNCE_MIN_BDY_SZ) {
        zxlogf(ERROR, "sdio_parse_func_ext: Invalid body size: %d for func_ext tuple\n",
               tup->t_body_size);
        return ZX_ERR_IO;
    }
    func->max_blk_size =  sdio_read_tuple_body(tup->t_body,
                                               SDIO_CIS_TPL_FUNCE_FUNCx_MAX_BLK_SIZE_LOC, 2);
    return ZX_OK;
}

static zx_status_t sdio_parse_mfid_tuple(sdmmc_device_t* dev, uint32_t fn_idx,
                                         sdio_func_tuple_t *tup) {
    if (tup->t_body_size < SDIO_CIS_TPL_MANFID_MIN_BDY_SZ) {
        return ZX_ERR_IO;
    }
    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    func->manufacturer_id = sdio_read_tuple_body(tup->t_body, 0, 2);
    func->product_id = sdio_read_tuple_body(tup->t_body, 2, 2);
    return ZX_OK;
}

static zx_status_t sdio_parse_fn_tuple(sdmmc_device_t* dev, uint32_t fn_idx,
                                       sdio_func_tuple_t *tup) {
    zx_status_t st = ZX_OK;
    switch (tup->t_code) {
        case SDIO_CIS_TPL_CODE_MANFID:
          st = sdio_parse_mfid_tuple(dev, fn_idx, tup);
          break;
        case SDIO_CIS_TPL_CODE_FUNCE:
          st = sdio_parse_func_ext_tuple(dev, fn_idx, tup);
          break;
        default:
          break;
    }
    return st;
}

static zx_status_t sdio_process_cis(sdmmc_device_t* dev, uint32_t fn_idx) {
    zx_status_t st = ZX_OK;

    if (fn_idx >= SDIO_MAX_FUNCS) {
        return ZX_ERR_INVALID_ARGS;
    }
    uint32_t cis_ptr = 0;
    for (size_t i = 0; i < SDIO_CIS_ADDRESS_SIZE; i++) {
        uint8_t addr;
        st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_FBR_BASE_ADDR(fn_idx) +
                               SDIO_CIA_FBR_CIS_ADDR + i, 0, &addr);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdio: Error reading CIS of CCCR reg: %d\n", st);
            return st;
        }
        cis_ptr |= addr << (i * 8);
    }
    if (!cis_ptr) {
        zxlogf(ERROR, "sdio: CIS address is invalid\n");
        return ZX_ERR_IO;
    }

    while (true) {
        uint8_t t_code, t_link;
        sdio_func_tuple_t cur_tup;
        st = sdio_io_rw_direct(dev, false, 0, cis_ptr + SDIO_CIS_TPL_FRMT_TCODE_OFF,
                               0, &t_code);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdio: Error reading tuple code for fn %d\n", fn_idx);
            break;
        }
        // Ignore null tuples
        if (t_code == SDIO_CIS_TPL_CODE_NULL) {
            cis_ptr++;
            continue;
        }
        if (t_code == SDIO_CIS_TPL_CODE_END) {
            break;
        }
        st = sdio_io_rw_direct(dev, false, 0, cis_ptr + SDIO_CIS_TPL_FRMT_TLINK_OFF,
                               0, &t_link);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdio: Error reading tuple size for fn %d\n", fn_idx);
            break;
        }
        if (t_link == SDIO_CIS_TPL_LINK_END) {
            break;
        }

        cur_tup.t_code = t_code;
        cur_tup.t_body_size = t_link;
        cur_tup.t_body = NULL;
        cur_tup.t_body = calloc(1, t_link);
        if (!(cur_tup.t_body)) {
            st = ZX_ERR_NO_MEMORY;
            break;
        }

        cis_ptr += SDIO_CIS_TPL_FRMT_TBODY_OFF;
        for (size_t i = 0; i < t_link; i++, cis_ptr++) {
            st = sdio_io_rw_direct(dev, false, 0, cis_ptr, 0, &(cur_tup.t_body[i]));
            if (st != ZX_OK) {
                zxlogf(ERROR, "sdio: Error reading tuple body for fn %d\n", fn_idx);
                free(cur_tup.t_body);
                return st;
            }
        }
        sdio_parse_fn_tuple(dev, fn_idx, &cur_tup);
        free(cur_tup.t_body);
    }
    return st;
}

static zx_status_t sdio_switch_hs(sdmmc_device_t *dev, bool enable) {
    zx_status_t st = ZX_OK;
    uint8_t speed = 0;

    if (!(dev->sdio_info.caps & SDIO_CARD_HIGH_SPEED)) {
        zxlogf(ERROR, "sdio: High speed not supported, retcode = %d\n", st);
        return ZX_ERR_NOT_SUPPORTED;
    }
    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, 0, &speed);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while reading CCCR reg, retcode = %d\n", st);
        return st;
    }
    speed = enable ? (speed | SDIO_BUS_SPEED_EN_HS) : (speed & ~SDIO_BUS_SPEED_EN_HS);
    st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, speed, NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while writing to CCCR reg, retcode = %d\n", st);
        return st;
    }
    // Switch the host timing
    if ((st = sdmmc_set_timing(&dev->host, SDMMC_TIMING_HS)) != ZX_OK) {
        zxlogf(ERROR, "sdio: failed to switch to hs timing on host : %d\n", st);
        return st;
    }
    return st;
}

static zx_status_t sdio_switch_freq(sdmmc_device_t* dev, uint32_t new_freq) {
    zx_status_t st;
    if ((st = sdmmc_set_bus_freq(&dev->host, new_freq)) != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while switching host bus frequency, retcode = %d\n", st);
        return st;
    }
    dev->clock_rate = new_freq;
    return ZX_OK;
}

static zx_status_t sdio_enable_4bit_bus(sdmmc_device_t *dev) {
    zx_status_t st = ZX_OK;
    if ((dev->sdio_info.caps & SDIO_CARD_LOW_SPEED) &&
        !(dev->sdio_info.caps & SDIO_CARD_4BIT_BUS)) {
        zxlogf(ERROR, "sdio: Switching to 4-bit bus unsupported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    uint8_t bus_ctrl_reg;
    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_INTF_CTRL_ADDR, 0,
                                &bus_ctrl_reg)) != ZX_OK) {
        zxlogf(INFO, "sdio: Error reading the current bus width\n");
        return st;
    }
    update_bits_u8(&bus_ctrl_reg, SDIO_CIA_CCCR_INTF_CTRL_BW_MASK, SDIO_CIA_CCCR_INTF_CTRL_BW_LOC,
                    SDIO_BW_4BIT);
    if ((st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_BUS_INTF_CTRL_ADDR, bus_ctrl_reg,
                                NULL)) != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while switching the bus width\n");
        return st;
    }
    if ((st = sdmmc_set_bus_width(&dev->host, SDMMC_BUS_WIDTH_4)) != ZX_OK) {
          zxlogf(ERROR, "sdio: failed to switch the host bus width to %d, retcode = %d\n",
                 SDMMC_BUS_WIDTH_4, st);
          return ZX_ERR_INTERNAL;
    }

    dev->bus_width = SDMMC_BUS_WIDTH_4;
    return ZX_OK;
}

static zx_status_t sdio_switch_bus_width(sdmmc_device_t *dev, uint32_t bw) {
    zx_status_t st = ZX_OK;
    if (bw != SDIO_BW_1BIT && bw != SDIO_BW_4BIT) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (bw == SDIO_BW_4BIT) {
        if ((st = sdio_enable_4bit_bus(dev)) != ZX_OK) {
            return st;
        }
    }
    return ZX_OK;
}

static zx_status_t sdio_process_fbr(sdmmc_device_t *dev, uint8_t fn_idx) {
    zx_status_t st = ZX_OK;
    uint8_t fbr, fn_intf_code;

    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_FBR_BASE_ADDR(fn_idx) +
                                SDIO_CIA_FBR_STD_IF_CODE_ADDR, 0, &fbr)) != ZX_OK) {
        zxlogf(ERROR, "sdio: Error reading intf code: %d\n", st);
        return st;
    }
    fn_intf_code = get_bits_u8(fbr, SDIO_CIA_FBR_STD_IF_CODE_MASK, SDIO_CIA_FBR_STD_IF_CODE_LOC);
    if (fn_intf_code == SDIO_CIA_FBR_STD_IF_CODE_MASK) {
        // fn_code > 0Eh
        if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_FBR_BASE_ADDR(fn_idx) +
                                    SDIO_CIA_FBR_STD_IF_CODE_EXT_ADDR, 0,
                                    &fn_intf_code)) != ZX_OK) {
            zxlogf(ERROR, "sdio: Error while reading the extended intf code %d\n", st);
            return st;
        }
    }
    func->fn_intf_code = fn_intf_code;
    return ZX_OK;
}

zx_status_t sdio_modify_block_size(void *ctx, uint8_t fn_idx, uint16_t blk_size,
                                         bool set_default) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = ctx;

    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    if (set_default) {
        blk_size = func->max_blk_size;
    }

    if (blk_size > func->max_blk_size) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (func->cur_blk_size == blk_size) {
        return ZX_OK;
    }

    st = sdio_write_data16(dev, 0, SDIO_CIA_FBR_BASE_ADDR(fn_idx) + SDIO_CIA_FBR_BLK_SIZE_ADDR,
                           blk_size);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_modify_block_size: Error writing to CCCR reg, retcode: %d\n", st);
        return st;
    }
    func->cur_blk_size = blk_size;
    return ZX_OK;
}

zx_status_t sdio_enable_function(void *ctx, uint8_t fn_idx) {
    uint8_t ioex_reg = 0;
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = ctx;

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    if (func->enabled) {
        return ZX_OK;
    }
    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, 0,
                                &ioex_reg)) != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_function: Error enabling func:%d status:%d\n",
               fn_idx, st);
        return st;
    }

    ioex_reg |= (1 << fn_idx);
    if ((st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, ioex_reg, NULL))
        != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_function: Error enabling func:%d status:%d\n",
               fn_idx, st);
        return st;
    }
    //wait for the device to enable the func.
    usleep(10 * 1000);
    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, 0,
                                &ioex_reg)) != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_function: Error enabling func:%d status:%d\n",
               fn_idx, st);
        return st;
    }

    if (!(ioex_reg & (1 << fn_idx))) {
        st = ZX_ERR_IO;
        zxlogf(ERROR, "sdio_enable_function: Failed to enable func %d\n", fn_idx);
        return st;
    }

    func->enabled = true;
    zxlogf(TRACE, "sdio_enable_function: Func %d is enabled\n", fn_idx);
    return st;
}

zx_status_t sdio_disable_function(void *ctx, uint8_t fn_idx) {
    uint8_t ioex_reg = 0;
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = ctx;

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdio_func_info_t *func = &(dev->sdio_info.funcs[fn_idx]);
    if (!func->enabled) {
        zxlogf(ERROR, "sdio_disable_function: Func %d is not enabled\n", fn_idx);
        return ZX_ERR_IO;
    }

    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, 0,
                                &ioex_reg)) != ZX_OK) {
        zxlogf(ERROR, "sdio_disable_function: Error reading IOEx reg. func: %d status: %d\n",
               fn_idx, st);
        return st;
    }

    ioex_reg &= ~(1 << fn_idx);
    if ((st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, ioex_reg, NULL))
        != ZX_OK) {
        zxlogf(ERROR, "sdio_disable_function: Error writing IOEx reg. func: %d status:%d\n",
               fn_idx, st);
        return st;
    }

    func->enabled = false;
    zxlogf(TRACE, "sdio_disable_function: Function %d is disabled\n", fn_idx);
    return st;
}

static zx_status_t sdio_init_func(sdmmc_device_t *dev, uint8_t fn_idx) {
    zx_status_t st = ZX_OK;

    if ((st = sdio_process_fbr(dev, fn_idx)) != ZX_OK) {
        return st;
    }

    if ((st = sdio_process_cis(dev, fn_idx)) != ZX_OK) {
        return st;
    }

    // Enable all func for now. Should move to wifi driver ?
    if ((st = sdio_enable_function(dev, fn_idx)) != ZX_OK) {
        return st;
    }

    // Set default block size
    if ((st = sdio_modify_block_size(dev, fn_idx, 0, true)) != ZX_OK) {
        return st;
    }

    return st;
}

zx_status_t sdmmc_probe_sdio(sdmmc_device_t* dev) {
    zx_status_t st = ZX_OK;
    uint32_t ocr;
    if ((st = sdio_send_op_cond(dev, 0, &ocr)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: SDIO_SEND_OP_COND failed, retcode = %d\n", st);
        return st;
    }
    //Select voltage 3.3 V. Also request for 1.8V. Section 3.2 SDIO spec
    if (ocr & SDIO_SEND_OP_COND_IO_OCR_33V) {
        uint32_t new_ocr = SDIO_SEND_OP_COND_IO_OCR_33V | SDIO_SEND_OP_COND_CMD_S18R;
        if ((st = sdio_send_op_cond(dev, new_ocr, &ocr)) != ZX_OK) {
            zxlogf(ERROR, "sdmmc_probe_sdio: SDIO_SEND_OP_COND failed, retcode = %d\n", st);
            return st;
        }
    }
    if (ocr & SDIO_SEND_OP_COND_RESP_MEM_PRESENT) {
        //TODO: Support combo cards later
        zxlogf(ERROR, "sdmmc_probe_sdio: Combo card not supported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    dev->type = SDMMC_TYPE_SDIO;
    dev->signal_voltage = SDMMC_VOLTAGE_180;
    dev->sdio_info.num_funcs = get_bits(ocr, SDIO_SEND_OP_COND_RESP_NUM_FUNC_MASK,
                                        SDIO_SEND_OP_COND_RESP_NUM_FUNC_LOC);
    uint16_t addr = 0;
    if ((st = sd_send_relative_addr(dev, &addr)) != ZX_OK) {
        zxlogf(ERROR, "sdmcc_probe_sdio: SD_SEND_RELATIVE_ADDR failed, retcode = %d\n", st);
        return st;
    }
    dev->rca = addr;
    if ((st = mmc_select_card(dev)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: MMC_SELECT_CARD failed, retcode = %d\n", st);
        return st;
    }

    //Read CIS to get max block size
    if ((st = sdio_process_cis(dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Read CIS failed, retcode = %d\n", st);
        return st;
    }

    if ((st = sdio_process_cccr(dev)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Read CCCR failed, retcode = %d\n", st);
        return st;
    }

    //TODO: Switch to UHS(Could not switch voltage to 1.8V).
    /*if (ocr & SDIO_SEND_OP_COND_RESP_S18A) {
        zxlogf(INFO, "sdmmc_probe_sdio Switching voltage to 1.8 V accepted.\n");
        if ((st = sd_switch_uhs_voltage(dev, ocr)) != ZX_OK) {
            zxlogf(INFO, "Failed to switch voltage to 1.8V\n");
            return st;
        }
    }*/

    sdio_modify_block_size(dev, 0, 0, true);
    if ((st = sdio_switch_hs(dev, true)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Switching to high speed failed, retcode = %d\n", st);
        return st;
    }

    //TODO: Setting this to 50 MHz fails the following I/O.May be because PORTA does
    //not operate at high frequency.
    /*uint32_t new_freq = 10000000;
    if ((st = sdio_switch_freq(dev, new_freq)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Switch freq retcode = %d\n", st);
        return st;
    }*/

    if ((st = sdio_switch_bus_width(dev, SDIO_BW_4BIT)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Swtiching to 4-bit bus width failed, retcode = %d\n", st);
    }

    // 0 is the common function. Already initialized
    for (size_t i = 1; i < dev->sdio_info.num_funcs; i++) {
        st = sdio_init_func(dev, i);
    }

    zxlogf(INFO, "sdmmc_probe_sdio: Manufacturer: 0x%x\n", dev->sdio_info.funcs[0].manufacturer_id);
    zxlogf(INFO, "                  Product: 0x%x\n", dev->sdio_info.funcs[0].product_id);
    zxlogf(INFO, "                  cccr vsn: 0x%x\n", dev->sdio_info.cccr_vsn);
    zxlogf(INFO, "                  SDIO vsn: 0x%x\n", dev->sdio_info.sdio_vsn);
    zxlogf(INFO, "                  num funcs: %d\n", dev->sdio_info.num_funcs);
    return ZX_OK;
}
