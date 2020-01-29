/* Copyright (c) 2014 Broadcom Corporation
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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_COMMON_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_COMMON_H_

#include <string>

#include "bus.h"
#include "core.h"
#include "fwil_types.h"
#include "linuxisms.h"

#define BRCMF_FW_ALTPATH_LEN 256
constexpr uint32_t kMaxAssocRetries = 3;

/**
 * struct brcmf_mp_device - Device module paramaters.
 *
 * @p2p_enable: Legacy P2P0 enable (old wpa_supplicant).
 * @feature_disable: Feature_disable bitmask.
 * @fcmode: FWS flow control.
 * @roamoff: Firmware roaming off?
 * @ignore_probe_fail: Ignore probe failure.
 * @country_codes: If available, pointer to struct for translating country codes
 * @bus: Bus specific platform data. Only SDIO at the mmoment.
 */
struct brcmf_mp_device {
  bool p2p_enable;
  unsigned int feature_disable;
  int fcmode;
  bool roamoff;
  bool ignore_probe_fail;
  struct brcmfmac_pd_cc* country_codes;
  struct {
    struct brcmf_sdio_pd* sdio;
  } bus;
};

void brcmf_gen_random_mac_addr(uint8_t* mac_addr);

void brcmf_c_set_joinpref_default(struct brcmf_if* ifp);

void brcmf_get_module_param(enum brcmf_bus_type bus_type, uint32_t chip, uint32_t chiprev,
                            brcmf_mp_device* settings);

zx_status_t brcmf_c_process_clm_blob(struct brcmf_if* ifp, std::string_view clm_binary);

/* Sets dongle media info (drv_version, mac address). */
zx_status_t brcmf_c_preinit_dcmds(struct brcmf_if* ifp);
zx_status_t brcmf_set_country(brcmf_pub* drvr, const wlanphy_country_t* country);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_COMMON_H_
