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
#include <zircon/process.h>
#include <zircon/threads.h>

#include "sdmmc.h"
#include "sdio.h"

static const uint32_t bcm_manufacturer_id = 0x02d0;

static inline void update_bits_u8(uint8_t *x, uint8_t mask, uint8_t loc, uint8_t val) {
    *x = static_cast<uint8_t>(*x & ~mask);
    *x = static_cast<uint8_t>(*x | ((val << loc) & mask));
}

static inline uint8_t get_bits_u8(uint8_t x, uint8_t mask, uint8_t loc) {
    return static_cast<uint8_t>((x & mask) >> loc);
}

static inline uint8_t get_bits(uint32_t x, uint32_t mask, uint32_t loc) {
    return static_cast<uint8_t>((x & mask) >> loc);
}

zx_status_t sdio_rw_byte(void *ctx, bool write, uint8_t fn_idx, uint32_t addr,
                         uint8_t write_byte, uint8_t *read_byte) {
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);
    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }
    read_byte = write ? NULL : read_byte;
    write_byte = write ? write_byte : 0;
    return sdio_io_rw_direct(dev, write, fn_idx, addr, write_byte, read_byte);
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

    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);
    zx_status_t st = ZX_OK;
    uint32_t addr = txn->addr;
    uint32_t data_size = txn->data_size;
    bool use_dma = txn->use_dma;

    // Single byte reads at some addresses are stuck when using io_rw_extended.
    // Use io_rw_direct whenever possible.
    if (!use_dma && data_size == 1) {
        return sdio_rw_byte(dev, txn->write, fn_idx, addr,
                            *reinterpret_cast<uint8_t*>(txn->virt_buffer),
                            reinterpret_cast<uint8_t*>(txn->virt_buffer));
    }

    if ((data_size % 4) != 0) {
        //TODO(ravoorir): This is definitely needed for PIO mode. Astro has
        //a hardware bug about not supporting DMA. We end up doing non-dma
        //transfers on astro.For now restrict the size for dma requests as well.
        zxlogf(ERROR, "sdio_rw_data: data size is not a multiple of 4\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    bool dma_supported = sdmmc_use_dma(dev);
    uint8_t *buf = use_dma ? NULL : reinterpret_cast<uint8_t*>(txn->virt_buffer);
    zx_handle_t dma_vmo = use_dma ? txn->dma_vmo : ZX_HANDLE_INVALID;
    uint64_t buf_offset = txn->buf_offset;

    if (txn->use_dma && !dma_supported) {
        // host does not support dma
        st = zx_vmar_map(zx_vmar_root_self(),
                         ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         0, txn->dma_vmo, txn->buf_offset, data_size,
                         (uintptr_t*)&buf);
        if (st != ZX_OK) {
            zxlogf(TRACE, "sdio_rw_data: vmo map error %d\n", st);
            return ZX_ERR_IO;
        }
        use_dma = false;
        dma_vmo = ZX_HANDLE_INVALID;
        buf_offset = 0; //set it to 0 since we mapped starting from offset.
    }

    bool mbs = (dev->sdio_dev.hw_info.caps) & SDIO_CARD_MULTI_BLOCK;
    uint32_t func_blk_size = (dev->sdio_dev.funcs[fn_idx]).cur_blk_size;
    uint32_t rem_blocks = (func_blk_size == 0) ? 0 : (data_size / func_blk_size);
    uint32_t data_processed = 0;
    while (rem_blocks > 0) {
        uint32_t num_blocks = 1;
        if (mbs) {
            uint32_t max_host_blocks;
            max_host_blocks = static_cast<uint32_t>(
                use_dma ? ((dev->host_info.max_transfer_size) / func_blk_size)
                        : ((dev->host_info.max_transfer_size_non_dma) / func_blk_size));
            // multiblock is supported, determine max number of blocks per cmd
            num_blocks = MIN(MIN(SDIO_IO_RW_EXTD_MAX_BLKS_PER_CMD, max_host_blocks), rem_blocks);
        }
        st = sdio_io_rw_extended(dev, txn->write, fn_idx, addr, txn->incr, num_blocks,
                                 func_blk_size, use_dma, buf, dma_vmo,
                                 buf_offset + data_processed);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdio_rw_data: Error %sing data.func: %d status: %d\n",
                   txn->write ? "writ" : "read", fn_idx, st);
            return st;
        }
        rem_blocks -= num_blocks;
        data_processed += num_blocks * func_blk_size;
        if (txn->incr) {
            addr += data_processed;
        }
    }

    if (data_processed < data_size) {
        // process remaining data.
        st = sdio_io_rw_extended(dev, txn->write, fn_idx, addr, txn->incr,
                                 1, (data_size - data_processed), use_dma, buf, dma_vmo,
                                 buf_offset + data_processed);
    }

    if (txn->use_dma && !dma_supported) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buf, data_size);
    }

    return st;
}

static zx_status_t sdio_read_data32(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                    uint32_t *dword) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = false;
    txn.virt_buffer = dword;
    txn.virt_size = 4;
    txn.data_size = 4;
    txn.incr = true;
    txn.use_dma = false;
    txn.buf_offset = 0;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_write_data32(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                     uint32_t dword) {
    sdio_rw_txn_t txn;
    txn.addr = addr;
    txn.write = true;
    txn.virt_buffer = (void *)&dword;
    txn.virt_size = 4;
    txn.data_size = 4;
    txn.incr = true;
    txn.use_dma = false;
    txn.buf_offset = 0;
    return sdio_rw_data(dev, fn_idx, &txn);
}

static zx_status_t sdio_read_data16(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                    uint16_t *word) {
    uint8_t byte1 = 0, byte2 = 0;
    zx_status_t st = sdio_rw_byte(dev, false, 0, addr, 0, &byte1);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_read_data16: Error reading from addr:0x%x, retcode: %d\n", addr, st);
        return st;
    }

    st = sdio_rw_byte(dev, false, 0, addr + 1, 0, &byte2);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_read_data16: Error reading from addr:0x%x, retcode: %d\n", addr + 1,
               st);
        return st;
    }

    *word = static_cast<uint16_t>(byte2 << 8 | byte1);
    return ZX_OK;
}

static zx_status_t sdio_write_data16(sdmmc_device_t *dev, uint8_t fn_idx, uint32_t addr,
                                     uint16_t word) {
    zx_status_t st = sdio_rw_byte(dev, true, 0, addr, static_cast<uint8_t>(word & 0xff), NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_write_data16: Error writing to addr:0x%x, retcode: %d\n", addr, st);
        return st;
    }

    st = sdio_rw_byte(dev, true, 0, addr + 1, static_cast<uint8_t>((word >> 8) & 0xff), NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_write_data16: Error writing to addr:0x%x, retcode: %d\n", addr + 1,
               st);
        return st;
    }

    return ZX_OK;
}

zx_status_t sdio_get_device_hw_info(void *ctx, sdio_hw_info_t *dev_info) {
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);
    sdio_device_t *sdio_dev = &(dev->sdio_dev);
    memcpy(&(dev_info->dev_hw_info), &(sdio_dev->hw_info), sizeof(sdio_device_hw_info_t));
    for (size_t i = 0; i < sdio_dev->hw_info.num_funcs; i++) {
        memcpy(&(dev_info->funcs_hw_info[i]), &(sdio_dev->funcs[i].hw_info),
                 sizeof(sdio_func_hw_info_t));
    }
    dev_info->host_max_transfer_size = static_cast<uint32_t>(dev->host_info.max_transfer_size);
    return ZX_OK;
}

static uint32_t sdio_read_tuple_body(uint8_t *t_body, size_t start, size_t numbytes) {
    uint32_t res = 0;

    for (size_t i = start; i < (start + numbytes); i++) {
        res |= t_body[i] << ((i - start)* 8);
    }
    return res;
}

static zx_status_t sdio_process_cccr(sdmmc_device_t *dev) {
    uint8_t cccr_vsn, sdio_vsn, vsn_info, bus_speed, card_caps, uhs_caps, drv_strength;

    //version info
    zx_status_t status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_CCCR_SDIO_VER_ADDR, 0, &vsn_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdio_process_cccr: Error reading CCCR reg: %d\n", status);
        return status;
    }
    cccr_vsn = get_bits(vsn_info, SDIO_CIA_CCCR_CCCR_VER_MASK, SDIO_CIA_CCCR_CCCR_VER_LOC);
    sdio_vsn = get_bits(vsn_info, SDIO_CIA_CCCR_SDIO_VER_MASK, SDIO_CIA_CCCR_SDIO_VER_LOC);
    if ((cccr_vsn < SDIO_CCCR_FORMAT_VER_3) || (sdio_vsn < SDIO_SDIO_VER_3)) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    dev->sdio_dev.hw_info.cccr_vsn = cccr_vsn;
    dev->sdio_dev.hw_info.sdio_vsn = sdio_vsn;

    //card capabilities
    status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_CARD_CAPS_ADDR, 0, &card_caps);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdio_process_cccr: Error reading CAPS reg: %d\n", status);
        return status;
    }
    dev->sdio_dev.hw_info.caps = 0;
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_SMB) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_MULTI_BLOCK;
    }
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_LSC) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_LOW_SPEED;
    }
    if (card_caps & SDIO_CIA_CCCR_CARD_CAP_4BLS) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_FOUR_BIT_BUS;
    }

    //speed
    status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, 0, &bus_speed);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdio_process_cccr: Error reading SPEED reg: %d\n", status);
        return status;
    }
    if (bus_speed & SDIO_CIA_CCCR_BUS_SPEED_SEL_SHS) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_HIGH_SPEED;
    }

    // Is UHS supported?
    status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_UHS_SUPPORT_ADDR, 0, &uhs_caps);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdio_process_cccr: Error reading SPEED reg: %d\n", status);
        return status;
    }
    if (uhs_caps & SDIO_CIA_CCCR_UHS_SDR50) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_UHS_SDR50;
    }
    if (uhs_caps & SDIO_CIA_CCCR_UHS_SDR104) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_UHS_SDR104;
    }
    if (uhs_caps & SDIO_CIA_CCCR_UHS_DDR50) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_UHS_DDR50;
    }

    //drv_strength
    status = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_DRV_STRENGTH_ADDR, 0,
                               &drv_strength);
    if (status != ZX_OK) {
        zxlogf(ERROR, "sdio_process_cccr: Error reading SPEED reg: %d\n", status);
        return status;
    }
    if (drv_strength & SDIO_CIA_CCCR_DRV_STRENGTH_SDTA) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_TYPE_A;
    }
    if (drv_strength & SDIO_CIA_CCCR_DRV_STRENGTH_SDTB) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_TYPE_B;
    }
    if (drv_strength & SDIO_CIA_CCCR_DRV_STRENGTH_SDTD) {
        dev->sdio_dev.hw_info.caps |= SDIO_CARD_TYPE_D;
    }
    return status;
}

static zx_status_t sdio_parse_func_ext_tuple(sdmmc_device_t* dev, uint32_t fn_idx,
                                             sdio_func_tuple_t *tup) {
    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    if (fn_idx == 0) {
        if (tup->t_body_size < SDIO_CIS_TPL_FUNC0_FUNCE_MIN_BDY_SZ) {
            return ZX_ERR_IO;
        }
        func->hw_info.max_blk_size = sdio_read_tuple_body(tup->t_body,
                                                  SDIO_CIS_TPL_FUNCE_FUNC0_MAX_BLK_SIZE_LOC, 2);
        func->hw_info.max_blk_size = static_cast<uint32_t>(MIN(dev->host_info.max_transfer_size,
                                                               func->hw_info.max_blk_size));
        uint8_t speed_val = get_bits_u8(tup->t_body[3], SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_MASK,
                                        SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_VAL_LOC);
        uint8_t speed_unit = get_bits_u8(tup->t_body[3],
                                         SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_MASK,
                                         SDIO_CIS_TPL_FUNCE_MAX_TRAN_SPEED_UNIT_LOC);
        func->hw_info.max_tran_speed = sdio_cis_tpl_funce_tran_speed_val[speed_val] *
                               sdio_cis_tpl_funce_tran_speed_unit[speed_unit];
        return ZX_OK;
    }

    if (tup->t_body_size < SDIO_CIS_TPL_FUNCx_FUNCE_MIN_BDY_SZ) {
        zxlogf(ERROR, "sdio_parse_func_ext: Invalid body size: %d for func_ext tuple\n",
               tup->t_body_size);
        return ZX_ERR_IO;
    }
    func->hw_info.max_blk_size =  sdio_read_tuple_body(tup->t_body,
                                               SDIO_CIS_TPL_FUNCE_FUNCx_MAX_BLK_SIZE_LOC, 2);
    return ZX_OK;
}

static zx_status_t sdio_parse_mfid_tuple(sdmmc_device_t* dev, uint32_t fn_idx,
                                         sdio_func_tuple_t *tup) {
    if (tup->t_body_size < SDIO_CIS_TPL_MANFID_MIN_BDY_SZ) {
        return ZX_ERR_IO;
    }
    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    func->hw_info.manufacturer_id = sdio_read_tuple_body(tup->t_body, 0, 2);
    func->hw_info.product_id = sdio_read_tuple_body(tup->t_body, 2, 2);
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
        st = sdio_io_rw_direct(dev, false, 0, static_cast<uint32_t>(SDIO_CIA_FBR_BASE_ADDR(fn_idx) +
                               SDIO_CIA_FBR_CIS_ADDR + i), 0, &addr);
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
        cur_tup.t_body = reinterpret_cast<uint8_t*>(calloc(1, t_link));
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

static zx_status_t sdio_switch_freq(sdmmc_device_t* dev, uint32_t new_freq) {
    zx_status_t st;
    if ((st = sdmmc_set_bus_freq(&dev->host, new_freq)) != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while switching host bus frequency, retcode = %d\n", st);
        return st;
    }
    dev->clock_rate = new_freq;
    return ZX_OK;
}

static zx_status_t sdio_switch_hs(sdmmc_device_t *dev) {
    zx_status_t st = ZX_OK;
    uint8_t speed = 0;

    if (!(dev->sdio_dev.hw_info.caps & SDIO_CARD_HIGH_SPEED)) {
        zxlogf(ERROR, "sdio: High speed not supported, retcode = %d\n", st);
        return ZX_ERR_NOT_SUPPORTED;
    }
    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, 0, &speed);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while reading CCCR reg, retcode = %d\n", st);
        return st;
    }
    update_bits_u8(&speed, SDIO_CIA_CCCR_BUS_SPEED_BSS_MASK, SDIO_CIA_CCCR_BUS_SPEED_BSS_LOC,
                   SDIO_BUS_SPEED_EN_HS);
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

    if ((st = sdio_switch_freq(dev, SDIO_HS_MAX_FREQ)) != ZX_OK) {
        zxlogf(ERROR, "sdio: failed to switch to hs timing on host : %d\n", st);
        return st;
    }
    return st;
}

static zx_status_t sdio_switch_uhs(sdmmc_device_t *dev) {
    zx_status_t st = ZX_OK;
    uint8_t speed = 0;
    uint32_t hw_caps = dev->sdio_dev.hw_info.caps;

    uint32_t new_freq = SDIO_DEFAULT_FREQ;
    uint8_t select_speed = SDIO_BUS_SPEED_SDR50;
    sdmmc_timing_t timing = SDMMC_TIMING_SDR50;

    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, 0, &speed);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while reading CCCR reg, retcode = %d\n", st);
        return st;
    }

    if (hw_caps & SDIO_CARD_UHS_SDR104) {
        select_speed = SDIO_BUS_SPEED_SDR104;
        timing = SDMMC_TIMING_SDR104;
        new_freq = SDIO_UHS_SDR104_MAX_FREQ;
    } else if (hw_caps & SDIO_CARD_UHS_SDR50) {
        select_speed = SDIO_BUS_SPEED_SDR50;
        timing = SDMMC_TIMING_SDR50;
        new_freq = SDIO_UHS_SDR50_MAX_FREQ;
    } else if (hw_caps & SDIO_CARD_UHS_DDR50) {
        select_speed = SDIO_BUS_SPEED_DDR50;
        timing = SDMMC_TIMING_DDR50;
        new_freq = SDIO_UHS_DDR50_MAX_FREQ;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }

    update_bits_u8(&speed, SDIO_CIA_CCCR_BUS_SPEED_BSS_MASK, SDIO_CIA_CCCR_BUS_SPEED_BSS_LOC,
                   select_speed);

    st = sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_BUS_SPEED_SEL_ADDR, speed, NULL);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio: Error while writing to CCCR reg, retcode = %d\n", st);
        return st;
    }
    // Switch the host timing
    if ((st = sdmmc_set_timing(&dev->host, timing)) != ZX_OK) {
        zxlogf(ERROR, "sdio: failed to switch to hs timing on host : %d\n", st);
        return st;
    }

    if ((st = sdio_switch_freq(dev, new_freq)) != ZX_OK) {
        zxlogf(ERROR, "sdio: failed to switch to hs timing on host : %d\n", st);
        return st;
    }
    return st;
}

static zx_status_t sdio_enable_4bit_bus(sdmmc_device_t *dev) {
    zx_status_t st = ZX_OK;
    if ((dev->sdio_dev.hw_info.caps & SDIO_CARD_LOW_SPEED) &&
        !(dev->sdio_dev.hw_info.caps & SDIO_CARD_FOUR_BIT_BUS)) {
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
    if ((st = sdmmc_set_bus_width(&dev->host, SDMMC_BUS_WIDTH_FOUR)) != ZX_OK) {
          zxlogf(ERROR, "sdio: failed to switch the host bus width to %d, retcode = %d\n",
                 SDMMC_BUS_WIDTH_FOUR, st);
          return ZX_ERR_INTERNAL;
    }

    dev->bus_width = SDMMC_BUS_WIDTH_FOUR;
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

    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
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
    func->hw_info.fn_intf_code = fn_intf_code;
    return ZX_OK;
}

zx_status_t sdio_get_cur_block_size(void *ctx, uint8_t fn_idx,
                                    uint16_t *cur_blk_size) {
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);

    zx_status_t st = sdio_read_data16(dev, 0,
                                      SDIO_CIA_FBR_BASE_ADDR(fn_idx) + SDIO_CIA_FBR_BLK_SIZE_ADDR,
                                      cur_blk_size);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_get_cur_block_size: Failed to get block size for fn: %d ret: %d\n",
               fn_idx, st);
    }
    return st;
}

zx_status_t sdio_modify_block_size(void *ctx, uint8_t fn_idx, uint16_t blk_size,
                                         bool set_default) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);

    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    if (set_default) {
        blk_size = static_cast<uint16_t>(func->hw_info.max_blk_size);
    }

    if (blk_size > func->hw_info.max_blk_size) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (func->cur_blk_size == blk_size) {
        return ZX_OK;
    }

    st = sdio_write_data16(dev, 0, SDIO_CIA_FBR_BASE_ADDR(fn_idx) + SDIO_CIA_FBR_BLK_SIZE_ADDR,
                           blk_size);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdio_modify_block_size: Error setting blk size.fn: %d blk_sz: %d ret: %d\n",
               fn_idx, blk_size, st);
        return st;
    }

    func->cur_blk_size = blk_size;
    return st;
}

zx_status_t sdio_enable_function(void *ctx, uint8_t fn_idx) {
    uint8_t ioex_reg = 0;
    zx_status_t st = ZX_OK;
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
    if (func->enabled) {
        return ZX_OK;
    }
    if ((st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_IOEx_EN_FUNC_ADDR, 0,
                                &ioex_reg)) != ZX_OK) {
        zxlogf(ERROR, "sdio_enable_function: Error enabling func:%d status:%d\n",
               fn_idx, st);
        return st;
    }

    ioex_reg = static_cast<uint8_t>(ioex_reg | (1 << fn_idx));
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
    sdmmc_device_t *dev = reinterpret_cast<sdmmc_device_t*>(ctx);

    if (!sdio_fn_idx_valid(fn_idx)) {
        return ZX_ERR_INVALID_ARGS;
    }

    sdio_function_t *func = &(dev->sdio_dev.funcs[fn_idx]);
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

    ioex_reg = static_cast<uint8_t>(ioex_reg & ~(1 << fn_idx));
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

zx_status_t sdmmc_sdio_reset(sdmmc_device_t* dev) {
    zx_status_t st = ZX_OK;
    uint8_t abort_byte;

    st = sdio_io_rw_direct(dev, false, 0, SDIO_CIA_CCCR_ASx_ABORT_SEL_CR_ADDR, 0, &abort_byte);
    if (st != ZX_OK) {
        abort_byte = SDIO_CIA_CCCR_ASx_ABORT_SOFT_RESET;
    } else {
        abort_byte |= SDIO_CIA_CCCR_ASx_ABORT_SOFT_RESET;
    }
    return sdio_io_rw_direct(dev, true, 0, SDIO_CIA_CCCR_ASx_ABORT_SEL_CR_ADDR, abort_byte, NULL);
}


zx_status_t sdmmc_probe_sdio(sdmmc_device_t* dev) {
    zx_status_t st = sdmmc_sdio_reset(dev);

    if ((st = sdmmc_go_idle(dev)) != ZX_OK) {
      zxlogf(ERROR, "sdmmc: SDMMC_GO_IDLE_STATE failed, retcode = %d\n", st);
      return st;
    }

    uint32_t ocr;
    if ((st = sdio_send_op_cond(dev, 0, &ocr)) != ZX_OK) {
        zxlogf(TRACE, "sdmmc_probe_sdio: SDIO_SEND_OP_COND failed, retcode = %d\n", st);
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
        //Combo cards not supported
        zxlogf(ERROR, "sdmmc_probe_sdio: Combo card not supported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    dev->type = SDMMC_TYPE_SDIO;
    dev->signal_voltage = SDMMC_VOLTAGE_V180;
    dev->sdio_dev.hw_info.num_funcs = get_bits(ocr, SDIO_SEND_OP_COND_RESP_NUM_FUNC_MASK,
                                               SDIO_SEND_OP_COND_RESP_NUM_FUNC_LOC);
    uint16_t addr = 0;
    if ((st = sd_send_relative_addr(dev, &addr, NULL)) != ZX_OK) {
        zxlogf(ERROR, "sdmcc_probe_sdio: SD_SEND_RELATIVE_ADDR failed, retcode = %d\n", st);
        return st;
    }
    dev->rca = addr;
    if ((st = mmc_select_card(dev)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: MMC_SELECT_CARD failed, retcode = %d\n", st);
        return st;
    }

    if ((st = sdio_process_cccr(dev)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Read CCCR failed, retcode = %d\n", st);
        return st;
    }

    //Read CIS to get max block size
    if ((st = sdio_process_cis(dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Read CIS failed, retcode = %d\n", st);
        return st;
    }

    if (ocr & SDIO_SEND_OP_COND_RESP_S18A) {
        if ((st = sd_switch_uhs_voltage(dev, ocr)) != ZX_OK) {
            zxlogf(INFO, "Failed to switch voltage to 1.8V\n");
            return st;
        }
    }

    // BCM43458 includes function 0 in its OCR register. This violates the SDIO specification and
    // the assumptions made here. Check the manufacturer ID to account for this quirk.
    if (dev->sdio_dev.funcs[0].hw_info.manufacturer_id != bcm_manufacturer_id) {
        dev->sdio_dev.hw_info.num_funcs++;
    }

    //TODO(ravoorir):Re-enable ultra high speed when wifi stack is more stable.
    /* if (sdio_is_uhs_supported(dev->sdio_dev.hw_info.caps)) {
        if ((st = sdio_switch_bus_width(dev, SDIO_BW_4BIT)) != ZX_OK) {
            zxlogf(ERROR, "sdmmc_probe_sdio: Swtiching to 4-bit bus width failed, retcode = %d\n",
                   st);
            goto high_speed;
        }
        if ((st = sdio_switch_uhs(dev)) != ZX_OK) {
            zxlogf(ERROR, "sdmmc_probe_sdio: Switching to high speed failed, retcode = %d\n", st);
            goto high_speed;
        }
        uint32_t hw_caps = dev->sdio_dev.hw_info.caps;

        if ((hw_caps & SDIO_CARD_UHS_SDR104) || (hw_caps & SDIO_CARD_UHS_SDR50)) {
            st = sdmmc_perform_tuning(&dev->host, SD_SEND_TUNING_BLOCK);
            if (st != ZX_OK) {
                zxlogf(ERROR, "mmc: tuning failed %d\n", st);
                goto high_speed;
            }
        }
        goto complete;
    }

high_speed: */
    if (dev->sdio_dev.hw_info.caps & SDIO_CARD_HIGH_SPEED) {
        if ((st = sdio_switch_hs(dev)) != ZX_OK) {
            zxlogf(ERROR, "sdmmc_probe_sdio: Switching to high speed failed, retcode = %d\n", st);
            goto default_speed;
        }

        if ((st = sdio_switch_bus_width(dev, SDIO_BW_4BIT)) != ZX_OK) {
            zxlogf(ERROR, "sdmmc_probe_sdio: Swtiching to 4-bit bus width failed, retcode = %d\n",
                   st);
            goto default_speed;
        }
        goto complete;
    }

default_speed:
    if ((st = sdio_switch_freq(dev, SDIO_DEFAULT_FREQ)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc_probe_sdio: Switch freq retcode = %d\n", st);
        return st;
    }

complete:
    sdio_modify_block_size(dev, 0, 0, true);
    // 0 is the common function. Already initialized
    for (size_t i = 1; i < dev->sdio_dev.hw_info.num_funcs; i++) {
        st = sdio_init_func(dev, static_cast<uint8_t>(i));
    }

    zxlogf(INFO, "sdmmc_probe_sdio: sdio device initialized successfully\n");
    zxlogf(INFO, "          Manufacturer: 0x%x\n", dev->sdio_dev.funcs[0].hw_info.manufacturer_id);
    zxlogf(INFO, "          Product: 0x%x\n", dev->sdio_dev.funcs[0].hw_info.product_id);
    zxlogf(INFO, "          cccr vsn: 0x%x\n", dev->sdio_dev.hw_info.cccr_vsn);
    zxlogf(INFO, "          SDIO vsn: 0x%x\n", dev->sdio_dev.hw_info.sdio_vsn);
    zxlogf(INFO, "          num funcs: %d\n", dev->sdio_dev.hw_info.num_funcs);
    return ZX_OK;
}
