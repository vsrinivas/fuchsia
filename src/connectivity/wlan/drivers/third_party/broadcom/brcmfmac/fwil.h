/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_H_

#include <wifi/wifi-config.h>

#include "core.h"
#include "fwil_types.h"

#define BRCMF_FWIL_BSSCFG_PREFIX "bsscfg:"

zx_status_t brcmf_fil_cmd_data_set(struct brcmf_if* ifp, uint32_t cmd, const void* data,
                                   uint32_t len, bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_cmd_data_get(struct brcmf_if* ifp, uint32_t cmd, void* data, uint32_t len,
                                   bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_cmd_int_set(struct brcmf_if* ifp, uint32_t cmd, uint32_t data,
                                  bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_cmd_int_get(struct brcmf_if* ifp, uint32_t cmd, uint32_t* data,
                                  bcme_status_t* fwerr_ptr);

zx_status_t brcmf_fil_iovar_data_set(struct brcmf_if* ifp, const char* name, const void* data,
                                     uint32_t len, bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_iovar_data_get(struct brcmf_if* ifp, const char* name, void* data,
                                     uint32_t len, bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_iovar_int_set(struct brcmf_if* ifp, const char* name, uint32_t data,
                                    bcme_status_t* fwerr_ptr);
zx_status_t brcmf_fil_iovar_int_get(struct brcmf_if* ifp, const char* name, uint32_t* data,
                                    bcme_status_t* fwerr_ptr);

zx_status_t brcmf_fil_bsscfg_data_set(struct brcmf_if* ifp, const char* name, const void* data,
                                      uint32_t len);
zx_status_t brcmf_fil_bsscfg_data_get(struct brcmf_if* ifp, const char* name, void* data,
                                      uint32_t len);
zx_status_t brcmf_fil_bsscfg_int_set(struct brcmf_if* ifp, const char* name, uint32_t data);
zx_status_t brcmf_fil_bsscfg_int_get(struct brcmf_if* ifp, const char* name, uint32_t* data);
const char* brcmf_fil_get_errstr(bcme_status_t err);
zx_status_t brcmf_send_cmd_to_firmware(brcmf_pub* drvr, uint32_t ifidx, uint32_t cmd, void* data,
                                       uint32_t len, bool set);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_H_
