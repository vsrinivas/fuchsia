/*
 * Copyright (c) 2010 Broadcom Corporation
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

#include "common.h"

#include <stdarg.h>
#include <string.h>
#include <sys/random.h>
#include <zircon/status.h>

#include <memory>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "calls.h"
#include "cfg80211.h"
#include "debug.h"
#include "device.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"

#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME 40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME 40

/* default boost value for RSSI_DELTA in preferred join selection */
#define BRCMF_JOIN_PREF_RSSI_BOOST 8

#define BRCMF_FW_NAME_LEN 256

// Disable features
static int brcmf_feature_disable;

// Mode of firmware signalled flow control
static int brcmf_fcmode;

// Do not use internal roaming engine
static bool brcmf_roamoff = 1;

#if !defined(NDEBUG)
/* always succeed brcmf_bus_started() for debugging */
static int brcmf_ignore_probe_fail;
#endif  // !defined(NDEBUG)

void brcmf_c_set_joinpref_default(struct brcmf_if* ifp) {
  struct brcmf_join_pref_params join_pref_params[2];
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  /* Setup join_pref to select target by RSSI (boost on 5GHz) */
  join_pref_params[0].type = BRCMF_JOIN_PREF_RSSI_DELTA;
  join_pref_params[0].len = 2;
  join_pref_params[0].rssi_gain = BRCMF_JOIN_PREF_RSSI_BOOST;
  join_pref_params[0].band = WLC_BAND_5G;

  join_pref_params[1].type = BRCMF_JOIN_PREF_RSSI;
  join_pref_params[1].len = 2;
  join_pref_params[1].rssi_gain = 0;
  join_pref_params[1].band = 0;
  err = brcmf_fil_iovar_data_set(ifp, "join_pref", join_pref_params, sizeof(join_pref_params),
                                 &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Set join_pref error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
}

// Read and send the CLM blob to firmware.
zx_status_t brcmf_c_process_clm_blob(struct brcmf_if* ifp, std::string_view clm_binary) {
  zx_status_t status = ZX_OK;

  const size_t dload_buf_size = sizeof(brcmf_dload_data_le) + MAX_CHUNK_LEN;
  std::unique_ptr<char[]> dload_buf(new char[dload_buf_size]);
  std::memset(dload_buf.get(), 0, dload_buf_size);

  brcmf_dload_data_le* const dload_data = reinterpret_cast<brcmf_dload_data_le*>(dload_buf.get());
  dload_data->flag = (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT) | DL_BEGIN;
  dload_data->dload_type = DL_TYPE_CLM;
  dload_data->crc = 0;

  for (size_t offset = 0; offset < clm_binary.size(); offset += MAX_CHUNK_LEN) {
    size_t chunk_len = MAX_CHUNK_LEN;
    if (clm_binary.size() - offset <= MAX_CHUNK_LEN) {
      chunk_len = clm_binary.size() - offset;
      dload_data->flag |= DL_END;
    }

    bcme_status_t fw_err = BCME_OK;
    std::memcpy(dload_data->data, clm_binary.data() + offset, chunk_len);
    dload_data->len = chunk_len;
    if ((status = brcmf_fil_iovar_data_set(ifp, "clmload", dload_data,
                                           sizeof(*dload_data) + chunk_len, &fw_err)) != ZX_OK) {
      BRCMF_ERR("clmload failed at offset %zu: %s, fw err %s", offset, zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
      return status;
    }

    dload_data->flag &= ~DL_BEGIN;
  }

  uint32_t clm_status = 0;
  bcme_status_t fw_err = BCME_OK;
  if ((status = brcmf_fil_iovar_int_get(ifp, "clmload_status", &clm_status, &fw_err)) != ZX_OK) {
    BRCMF_ERR("get clmload_status failed: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    return status;
  } else {
    // If status is non-zero, CLM load failed, return error back to caller.
    if (clm_status != 0) {
      BRCMF_ERR("clmload failed status=%d", status);
      return ZX_ERR_IO;
    }
  }

  return ZX_OK;
}

zx_status_t brcmf_gen_random_mac_addr(uint8_t* mac_addr) {
  int err = BRCMF_CALL(getentropy, mac_addr, ETH_ALEN);
  if (err != 0) {
    // The only reason getentropy() should fail is if we asked for more bytes than it is willing to
    // provide in one go. We don't have a backup plan for this.
    BRCMF_ERR("getentropy failed with status %d", err);
    return ZX_ERR_INTERNAL;
  }

  mac_addr[0] &= 0xfe;  // bit 0: 0 = unicast
  mac_addr[0] |= 0x02;  // bit 1: 1 = locally-administered
  return ZX_OK;
}

zx_status_t brcmf_set_macaddr_from_firmware(struct brcmf_if* ifp) {
  // Use static MAC address defined in the firmware.
  // eg. "macaddr" field of brcmfmac43455-sdio.txt
  uint8_t mac_addr[ETH_ALEN];
  bcme_status_t fw_err = BCME_OK;

  zx_status_t err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", mac_addr, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to retrieve mac address from firmware: %s, fw err %s",
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    return err;
  }

  memcpy(ifp->mac_addr, mac_addr, sizeof(ifp->mac_addr));
  return ZX_OK;
}

static zx_status_t brcmf_set_macaddr(struct brcmf_if* ifp) {
  uint8_t mac_addr[ETH_ALEN];
  bcme_status_t fw_err = BCME_OK;
  zx_status_t err = brcmf_bus_get_bootloader_macaddr(ifp->drvr->bus_if, mac_addr);
  if (err != ZX_OK) {
    // If desired, fall back to firmware mac address
    // by using brcmf_set_macaddr_from_firmware();

    // Fallback to a random mac address.
    BRCMF_ERR("Failed to get mac address from bootloader. Falling back to random mac address");
    err = brcmf_gen_random_mac_addr(mac_addr);
    if (err != ZX_OK) {
      return err;
    }
    BRCMF_ERR("random mac address to be assigned: %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
              mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  }

  err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", mac_addr, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to set mac address: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    return err;
  }

  memcpy(ifp->mac_addr, mac_addr, sizeof(ifp->mac_addr));
  return ZX_OK;
}

// Get Broadcom WiFi Metadata by calling the bus specific function
zx_status_t brcmf_get_meta_data(brcmf_if* ifp, wifi_config_t* config) {
  zx_status_t err;
  size_t actual;
  err = brcmf_bus_get_wifi_metadata(ifp->drvr->bus_if, config, sizeof(wifi_config_t), &actual);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to retrieve wifi metadata: %s", zx_status_get_string(err));
    memset(config, 0, sizeof(*config));
    return err;
  }
  if (actual != sizeof(*config)) {
    BRCMF_ERR("meta data size err exp:%lu act: %lu", sizeof(*config), actual);
    memset(config, 0, sizeof(*config));
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

/* Search through the given country code table and issue the iovar */
zx_status_t brcmf_set_country(brcmf_pub* drvr, const wlanphy_country_t* country) {
  if (country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct brcmf_if* ifp = brcmf_get_ifp(drvr, 0);
  wifi_config_t config;
  struct brcmf_fil_country_le ccreq;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;
  const unsigned char* code = country->alpha2;
  int i;

  BRCMF_DBG(TRACE, "Enter: code=%c%c", code[0], code[1]);
  // Get Broadcom WiFi Metadata by calling the bus specific function
  err = brcmf_get_meta_data(ifp, &config);
  if (err != ZX_OK) {
    return err;
  }

  // This is the default value in case the relevant entry is not found in the table.
  ccreq.rev = 0;
  // Search through the table until a valid or Null entry is found
  for (i = 0; i < MAX_CC_TABLE_ENTRIES; i++) {
    if (config.cc_table[i].cc_abbr[0] == 0) {
      BRCMF_ERR("Failed to find ccode %c%c in table", code[0], code[1]);
      return ZX_ERR_NOT_FOUND;
    }
    if (memcmp(config.cc_table[i].cc_abbr, code, WLANPHY_ALPHA2_LEN) == 0) {
      ccreq.rev = config.cc_table[i].cc_rev;
      break;
    }
  }
  // It appears brcm firmware expects ccode and country_abbrev to have the same value
  ccreq.ccode[0] = code[0];
  ccreq.ccode[1] = code[1];
  ccreq.ccode[2] = 0;
  ccreq.country_abbrev[0] = code[0];
  ccreq.country_abbrev[1] = code[1];
  ccreq.country_abbrev[2] = 0;

  // Log out the country code settings for reference
  BRCMF_INFO("Country code set ccode %s, abbrev %s, rev %d", ccreq.ccode, ccreq.country_abbrev,
             ccreq.rev);
  // Set the country info in firmware
  err = brcmf_fil_iovar_data_set(ifp, "country", &ccreq, sizeof(ccreq), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Firmware rejected country setting: %s fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
  return err;
}

/* Retrieve the current country code from the firmware */
zx_status_t brcmf_get_country(brcmf_pub* drvr, wlanphy_country_t* out_country) {
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, 0);
  struct brcmf_fil_country_le ccreq;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  // Get country info from firmware
  memset(&ccreq, 0, sizeof(ccreq));
  err = brcmf_fil_iovar_data_get(ifp, "country", &ccreq, sizeof(ccreq), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Firmware rejected country read: %s fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    return err;
  }

  // Log out the country code settings for reference
  BRCMF_INFO("Country code get ccode %s, abbrev %s, rev %d", ccreq.ccode, ccreq.country_abbrev,
             ccreq.rev);
  memcpy(out_country->alpha2, ccreq.ccode, WLANPHY_ALPHA2_LEN);
  return ZX_OK;
}

/* Set firmware country code to a world-safe one, which is "WW" in brcmfmac*/
zx_status_t brcmf_clear_country(brcmf_pub* drvr) {
  wlanphy_country_t country = {};
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter");
  country.alpha2[0] = country.alpha2[1] = 'W';

  err = brcmf_set_country(drvr, &country);
  if (err != ZX_OK) {
    BRCMF_ERR("Clear country failed");
  } else {
    BRCMF_DBG(INFO, "Clear country success");
  }

  return err;
}

// This function applies configured platform specific iovars to the firmware
static void brcmf_set_init_cfg_params(brcmf_if* ifp) {
  int i;
  bcme_status_t fw_err;
  zx_status_t err;
  wifi_config_t config;

  err = brcmf_get_meta_data(ifp, &config);
  if (err != ZX_OK) {
    return;
  }
  // Go through the table until a null entry is found
  for (i = 0; i < MAX_IOVAR_ENTRIES; i++) {
    switch (config.iovar_table[i].iovar_type) {
      case IOVAR_STR_TYPE: {
        uint32_t cur_val;

        // First, get the current value (for debugging)
        err = brcmf_fil_iovar_int_get(ifp, config.iovar_table[i].iovar_str, &cur_val, &fw_err);
        if (err != ZX_OK) {
          BRCMF_ERR("iovar get error: %s, fw err %s", config.iovar_table[i].iovar_str,
                    brcmf_fil_get_errstr(fw_err));
        } else {
          BRCMF_INFO("iovar %s get: %d new: %d", config.iovar_table[i].iovar_str, cur_val,
                     config.iovar_table[i].val);
          err = brcmf_fil_iovar_int_set(ifp, config.iovar_table[i].iovar_str,
                                        config.iovar_table[i].val, &fw_err);
          if (err != ZX_OK) {
            BRCMF_ERR("iovar set error: %s, fw err %s", config.iovar_table[i].iovar_str,
                      brcmf_fil_get_errstr(fw_err));
          }
        }
        break;
      }
      case IOVAR_CMD_TYPE: {
        err =
            brcmf_fil_cmd_data_set(ifp, config.iovar_table[i].iovar_cmd, &config.iovar_table[i].val,
                                   sizeof(config.iovar_table[i].val), &fw_err);
        if (err != ZX_OK) {
          BRCMF_ERR("iovar cmd set error: %d, fw err %s", config.iovar_table[i].iovar_cmd,
                    brcmf_fil_get_errstr(fw_err));
        }
      }
      case IOVAR_LIST_END_TYPE:
        // End of list, done setting iovars
        return;
    }
  }
}

zx_status_t brcmf_c_preinit_dcmds(struct brcmf_if* ifp) {
  int8_t eventmask[BRCMF_EVENTING_MASK_LEN];
  uint8_t buf[BRCMF_DCMD_SMLEN];
  struct brcmf_rev_info_le revinfo;
  struct brcmf_rev_info* ri;
  char* clmver;
  char* ptr;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;
  const wlanphy_country_t country = {{'U', 'S'}};

  err = brcmf_set_macaddr(ifp);
  if (err != ZX_OK) {
    goto done;
  }

  err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_REVINFO, &revinfo, sizeof(revinfo), &fw_err);
  ri = &ifp->drvr->revinfo;
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to retrieve revision info: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  } else {
    memcpy(&ri->fwrevinfo, &revinfo, sizeof(revinfo));
  }
  ri->result = err;

  /* query for 'ver' to get version info from firmware */
  memset(buf, 0, sizeof(buf));
  strcpy((char*)buf, "ver");
  err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to retrieve version information: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto done;
  } else {
    /* Print fw version info */
    BRCMF_INFO("Firmware version = %s", buf);
    ptr = (char*)buf;
    strsep(&ptr, "\n");

    /* locate firmware version number for ethtool */
    ptr = strrchr((char*)buf, ' ') + 1;
    strlcpy(ifp->drvr->fwver, ptr, sizeof(ifp->drvr->fwver));
  }

  /* Query for 'clmver' to get CLM version info from firmware */
  memset(buf, 0, sizeof(buf));
  err = brcmf_fil_iovar_data_get(ifp, "clmver", buf, sizeof(buf), &fw_err);
  if (err != ZX_OK) {
    BRCMF_DBG(INFO, "Failed to retrieve clmver: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  } else {
    clmver = (char*)buf;
    /* store CLM version for adding it to revinfo debugfs file */
    memcpy(ifp->drvr->clmver, clmver, sizeof(ifp->drvr->clmver));

    /* Replace all newline/linefeed characters with space
     * character
     */
    clmver[sizeof(buf) - 1] = 0;
    ptr = clmver;
    while ((ptr = strchr(ptr, '\n')) != NULL) {
      *ptr = ' ';
    }

    // Print out the CLM version to the log
    BRCMF_INFO("CLM version = %s", clmver);
  }

  brcmf_set_country(ifp->drvr, &country);
  brcmf_set_init_cfg_params(ifp);

  brcmf_c_set_joinpref_default(ifp);

  /* Setup event_msgs, enable E_IF */
  err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Get event_msgs error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto done;
  }

  setbit(eventmask, BRCMF_E_IF);
  err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Set event_msgs error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto done;
  }

  /* Setup default scan channel time */
  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME, BRCMF_DEFAULT_SCAN_CHANNEL_TIME,
                              &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("BRCMF_C_SET_SCAN_CHANNEL_TIME error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto done;
  }

  /* Setup default scan unassoc time */
  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME, BRCMF_DEFAULT_SCAN_UNASSOC_TIME,
                              &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("BRCMF_C_SET_SCAN_UNASSOC_TIME error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto done;
  }

  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("BRCMF_C_DOWN error %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
  // Enable simultaneous STA/AP operation, aka Real Simultaneous Dual Band (RSDB)
  err = brcmf_fil_iovar_int_set(ifp, "apsta", 1, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Set apsta error %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("BRCMF_C_UP error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
  /* Enable tx beamforming, errors can be ignored (not supported) */
  (void)brcmf_fil_iovar_int_set(ifp, "txbf", 1, nullptr);

  // Enable additional retries of association request at the firmware. This is a nice to have
  // feature. Ignore if the iovar fails.
  err = brcmf_fil_iovar_data_set(ifp, "assoc_retry_max", &kMaxAssocRetries,
                                 sizeof(kMaxAssocRetries), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to set assoc_retry_max: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  /* do bus specific preinit here */
  err = brcmf_bus_preinit(ifp->drvr->bus_if);
done:
  return err;
}

void brcmf_get_module_param(enum brcmf_bus_type bus_type, uint32_t chip, uint32_t chiprev,
                            brcmf_mp_device* settings) {
  /* start by using the module paramaters */
  settings->feature_disable = brcmf_feature_disable;
  settings->fcmode = brcmf_fcmode;
  settings->roamoff = brcmf_roamoff;
#if !defined(NDEBUG)
  settings->ignore_probe_fail = !!brcmf_ignore_probe_fail;
#endif  // !defined(NDEBUG)

#ifdef USE_PLATFORM_DATA
  // TODO(WLAN-731): Do we need to do this?
  struct brcmfmac_pd_device {
    uint32_t bus_type;
    uint32_t id;
    int rev;
    struct brcmfmac_pd_cc country_codes[555];
    struct {
      void* sdio;
    } bus;
  };

  struct brcmfmac_pd_device* device_pd;
  bool found;
  int i;

  /* See if there is any device specific platform data configured */
  found = false;
  if (brcmfmac_pdata) {
    for (i = 0; i < brcmfmac_pdata->device_count; i++) {
      device_pd = &brcmfmac_pdata->devices[i];
      if ((device_pd->bus_type == bus_type) && (device_pd->id == chip) &&
          ((device_pd->rev == (int32_t)chiprev) || (device_pd->rev == -1))) {
        BRCMF_DBG(INFO, "Platform data for device found");
        settings->country_codes = device_pd->country_codes;
        if (device_pd->bus_type == BRCMF_BUS_TYPE_SDIO) {
          memcpy(&settings->bus.sdio, &device_pd->bus.sdio, sizeof(settings->bus.sdio));
        }
        found = true;
        break;
      }
    }
  }
#endif /* USE_PLATFORM_DATA */
}
