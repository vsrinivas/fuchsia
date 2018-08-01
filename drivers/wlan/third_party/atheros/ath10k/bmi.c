/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bmi.h"

#include <ddk/driver.h>
#include <zircon/status.h>

#include <string.h>

#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "hw.h"

void ath10k_bmi_start(struct ath10k* ar) {
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi start\n");

    ar->bmi.done_sent = false;

    /* Enable hardware clock to speed up firmware download */
    if (ar->hw_params.hw_ops->enable_pll_clk) {
        ret = ar->hw_params.hw_ops->enable_pll_clk(ar);
        ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi enable pll ret %s\n", zx_status_get_string(ret));
    }
}

zx_status_t ath10k_bmi_done(struct ath10k* ar) {
    struct bmi_cmd cmd;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.done);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi done\n");

    if (ar->bmi.done_sent) {
        ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi skipped\n");
        return ZX_OK;
    }

    ar->bmi.done_sent = true;
    cmd.id = BMI_DONE;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
    if (ret != ZX_OK) {
        ath10k_warn("unable to write to the device: %s\n", zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

zx_status_t ath10k_bmi_get_target_info(struct ath10k* ar,
                                       struct bmi_target_info* target_info) {
    struct bmi_cmd cmd;
    union bmi_resp resp;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.get_target_info);
    uint32_t resplen = sizeof(resp.get_target_info);
    int ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi get target info\n");

    if (ar->bmi.done_sent) {
        ath10k_warn("BMI Get Target Info Command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    cmd.id = BMI_GET_TARGET_INFO;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
    if (ret) {
        ath10k_warn("unable to get target info from device: %s\n",
                    zx_status_get_string(ret));
        return ret;
    }

    if (resplen < sizeof(resp.get_target_info)) {
        ath10k_warn("invalid get_target_info response length (%d)\n",
                    resplen);
        return ZX_ERR_IO;
    }

    target_info->version = resp.get_target_info.version;
    target_info->type    = resp.get_target_info.type;

    return ZX_OK;
}

#define TARGET_VERSION_SENTINAL 0xffffffffu

zx_status_t ath10k_bmi_get_target_info_sdio(struct ath10k* ar,
                                            struct bmi_target_info* target_info) {
    struct bmi_cmd cmd;
    union bmi_resp resp;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.get_target_info);
    uint32_t resplen, ver_len;
    uint32_t tmp;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi get target info SDIO\n");

    if (ar->bmi.done_sent) {
        ath10k_warn("BMI Get Target Info Command disallowed\n");
        return ZX_ERR_SHOULD_WAIT;
    }

    cmd.id = BMI_GET_TARGET_INFO;

    /* Step 1: Read 4 bytes of the target info and check if it is
     * the special sentinal version word or the first word in the
     * version response.
     */
    resplen = sizeof(uint32_t);
    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &tmp, &resplen);
    if (ret) {
        ath10k_warn("unable to read from device\n");
        return ret;
    }

    /* Some SDIO boards have a special sentinal byte before the real
     * version response.
     */
    if (tmp == TARGET_VERSION_SENTINAL) {
        /* Step 1b: Read the version length */
        resplen = sizeof(uint32_t);
        ret = ath10k_hif_exchange_bmi_msg(ar, NULL, 0, &tmp,
                                          &resplen);
        if (ret) {
            ath10k_warn("unable to read from device\n");
            return ret;
        }
    }

    ver_len = tmp;

    /* Step 2: Check the target info length */
    if (ver_len != sizeof(resp.get_target_info)) {
        ath10k_warn("Unexpected target info len: %u. Expected: %zu\n",
                    ver_len, sizeof(resp.get_target_info));
        return ZX_ERR_WRONG_TYPE;
    }

    /* Step 3: Read the rest of the version response */
    resplen = sizeof(resp.get_target_info) - sizeof(uint32_t);
    ret = ath10k_hif_exchange_bmi_msg(ar, NULL, 0,
                                      &resp.get_target_info.version,
                                      &resplen);
    if (ret) {
        ath10k_warn("unable to read from device\n");
        return ret;
    }

    target_info->version = resp.get_target_info.version;
    target_info->type    = resp.get_target_info.type;

    return ZX_OK;
}

zx_status_t ath10k_bmi_read_memory(struct ath10k* ar, uint32_t address,
                                   void* buffer, uint32_t length) {
    struct bmi_cmd cmd;
    union bmi_resp resp;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.read_mem);
    uint32_t rxlen;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read address 0x%x length %d\n",
               address, length);

    if (ar->bmi.done_sent) {
        ath10k_warn("command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    while (length) {
        rxlen = MIN_T(uint32_t, length, BMI_MAX_DATA_SIZE);

        cmd.id            = BMI_READ_MEMORY;
        cmd.read_mem.addr = address;
        cmd.read_mem.len  = rxlen;

        ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen,
                                          &resp, &rxlen);
        if (ret != ZX_OK) {
            ath10k_warn("unable to read from the device (%s)\n",
                        zx_status_get_string(ret));
            return ret;
        }

        memcpy(buffer, resp.read_mem.payload, rxlen);
        address += rxlen;
        buffer  += rxlen;
        length  -= rxlen;
    }

    return ZX_OK;
}

zx_status_t ath10k_bmi_write_soc_reg(struct ath10k* ar, uint32_t address, uint32_t reg_val) {
    struct bmi_cmd cmd;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.write_soc_reg);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI,
               "bmi write soc register 0x%08x val 0x%08x\n",
               address, reg_val);

    if (ar->bmi.done_sent) {
        ath10k_warn("bmi write soc register command in progress\n");
        return ZX_ERR_BAD_STATE;
    }

    cmd.id = BMI_WRITE_SOC_REGISTER;
    cmd.write_soc_reg.addr = address;
    cmd.write_soc_reg.value = reg_val;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
    if (ret != ZX_OK) {
        ath10k_warn("Unable to write soc register to device: %s\n",
                    zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

int ath10k_bmi_read_soc_reg(struct ath10k* ar, uint32_t address, uint32_t* reg_val) {
    struct bmi_cmd cmd;
    union bmi_resp resp;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.read_soc_reg);
    uint32_t resplen = sizeof(resp.read_soc_reg);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read soc register 0x%08x\n",
               address);

    if (ar->bmi.done_sent) {
        ath10k_warn("bmi read soc register command in progress\n");
        return ZX_ERR_BAD_STATE;
    }

    cmd.id = BMI_READ_SOC_REGISTER;
    cmd.read_soc_reg.addr = address;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
    if (ret) {
        ath10k_warn("Unable to read soc register from device: %s\n",
                    zx_status_get_string(ret));
        return ret;
    }

    *reg_val = resp.read_soc_reg.value;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi read soc register value 0x%08x\n",
               *reg_val);

    return ZX_OK;
}

zx_status_t ath10k_bmi_write_memory(struct ath10k* ar, uint32_t address,
                                    const void* buffer, uint32_t length) {
    struct bmi_cmd cmd;
    uint32_t hdrlen = sizeof(cmd.id) + sizeof(cmd.write_mem);
    uint32_t txlen;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi write address 0x%x length %d\n",
               address, length);

    if (ar->bmi.done_sent) {
        ath10k_warn("command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    while (length) {
        txlen = MIN(length, BMI_MAX_DATA_SIZE - hdrlen);

        /* copy before roundup to avoid reading beyond buffer*/
        memcpy(cmd.write_mem.payload, buffer, txlen);
        txlen = ROUNDUP(txlen, 4);

        cmd.id             = BMI_WRITE_MEMORY;
        cmd.write_mem.addr = address;
        cmd.write_mem.len  = txlen;

        ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, hdrlen + txlen,
                                          NULL, NULL);
        if (ret) {
            ath10k_warn("unable to write to the device (%s)\n",
                        zx_status_get_string(ret));
            return ret;
        }

        /* fixup ROUNDUP() so `length` zeroes out for last chunk */
        txlen = MIN(txlen, length);

        address += txlen;
        buffer  += txlen;
        length  -= txlen;
    }

    return ZX_OK;
}

zx_status_t ath10k_bmi_execute(struct ath10k* ar, uint32_t address,
                               uint32_t param, uint32_t* result) {
    struct bmi_cmd cmd;
    union bmi_resp resp;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.execute);
    uint32_t resplen = sizeof(resp.execute);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi execute address 0x%x param 0x%x\n",
               address, param);

    if (ar->bmi.done_sent) {
        ath10k_warn("command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    cmd.id            = BMI_EXECUTE;
    cmd.execute.addr  = address;
    cmd.execute.param = param;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
    if (ret != ZX_OK) {
        ath10k_warn("unable to read from the device\n");
        return ret;
    }

    if (resplen < sizeof(resp.execute)) {
        ath10k_warn("invalid execute response length (%d)\n",
                    resplen);
        return ZX_ERR_IO;
    }

    *result = resp.execute.result;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi execute result 0x%x\n", *result);

    return ZX_OK;
}

zx_status_t ath10k_bmi_lz_data(struct ath10k* ar, const void* buffer, uint32_t length) {
    struct bmi_cmd cmd;
    uint32_t hdrlen = sizeof(cmd.id) + sizeof(cmd.lz_data);
    uint32_t txlen;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi lz data buffer 0x%pK length %d\n",
               buffer, length);

    if (ar->bmi.done_sent) {
        ath10k_warn("command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    while (length) {
        txlen = MIN(length, BMI_MAX_DATA_SIZE - hdrlen);

        COND_WARN_ONCE(txlen & 3);

        cmd.id          = BMI_LZ_DATA;
        cmd.lz_data.len = txlen;
        memcpy(cmd.lz_data.payload, buffer, txlen);

        ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, hdrlen + txlen,
                                          NULL, NULL);
        if (ret != ZX_OK) {
            ath10k_warn("unable to write to the device\n");
            return ret;
        }

        buffer += txlen;
        length -= txlen;
    }

    return ZX_OK;
}

zx_status_t ath10k_bmi_lz_stream_start(struct ath10k* ar, uint32_t address) {
    struct bmi_cmd cmd;
    uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.lz_start);
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi lz stream start address 0x%x\n",
               address);

    if (ar->bmi.done_sent) {
        ath10k_warn("command disallowed\n");
        return ZX_ERR_BAD_STATE;
    }

    cmd.id            = BMI_LZ_STREAM_START;
    cmd.lz_start.addr = address;

    ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, NULL, NULL);
    if (ret != ZX_OK) {
        ath10k_warn("unable to Start LZ Stream to the device\n");
        return ret;
    }

    return ZX_OK;
}

zx_status_t ath10k_bmi_fast_download(struct ath10k* ar, uint32_t address,
                                     const void* buffer, uint32_t length) {
    uint8_t trailer[4] = {};
    uint32_t head_len = ROUNDDOWN(length, 4);
    uint32_t trailer_len = length - head_len;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BMI,
               "bmi fast download address 0x%x buffer 0x%pK length %d\n",
               address, buffer, length);

    ret = ath10k_bmi_lz_stream_start(ar, address);
    if (ret != ZX_OK) {
        return ret;
    }

    /* copy the last word into a zero padded buffer */
    if (trailer_len > 0) {
        memcpy(trailer, buffer + head_len, trailer_len);
    }

    ret = ath10k_bmi_lz_data(ar, buffer, head_len);
    if (ret != ZX_OK) {
        return ret;
    }

    if (trailer_len > 0) {
        ret = ath10k_bmi_lz_data(ar, trailer, 4);
    }

    if (ret != ZX_OK) {
        return ret;
    }

    /*
     * Close compressed stream and open a new (fake) one.
     * This serves mainly to flush Target caches.
     */
    ret = ath10k_bmi_lz_stream_start(ar, 0x00);

    return ret;
}
