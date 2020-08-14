/*
 * Copyright (c) 2014 Broadcom Corporation
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

#include "feature.h"

#include <string.h>
#include <zircon/status.h>

#include "brcm_hw_ids.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "third_party/bcmdhd/crossdriver/dhd.h"

/*
 * expand feature list to array of feature strings.
 */
#define BRCMF_FEAT_DEF(_f) #_f,
static const char* brcmf_feat_names[] = {BRCMF_FEAT_LIST};
#undef BRCMF_FEAT_DEF

struct brcmf_feat_fwcap {
  enum brcmf_feat_id feature;
  const char* const fwcap_id;
};

static const struct brcmf_feat_fwcap brcmf_fwcap_map[] = {
    {BRCMF_FEAT_AP, "ap"},          {BRCMF_FEAT_STA, "sta"},     {BRCMF_FEAT_MBSS, "mbss"},
    {BRCMF_FEAT_MCHAN, "mchan"},    {BRCMF_FEAT_P2P, "p2p"},     {BRCMF_FEAT_PNO, "pno"},
    {BRCMF_FEAT_EPNO, "epno"},      {BRCMF_FEAT_DFS, "802.11h"}, {BRCMF_FEAT_TPC, "802.11h"},
    {BRCMF_FEAT_DOT11H, "802.11h"},
};

/**
 * brcmf_feat_iovar_int_get() - determine feature through iovar query.
 *
 * @ifp: interface to query.
 * @id: feature id.
 * @name: iovar name.
 */
static void brcmf_feat_iovar_int_get(struct brcmf_if* ifp, enum brcmf_feat_id id,
                                     const char* name) {
  uint32_t data;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  err = brcmf_fil_iovar_int_get(ifp, name, &data, &fw_err);
  if (err == ZX_OK) {
    BRCMF_DBG(INFO, "enabling feature: %s", brcmf_feat_names[id]);
    ifp->drvr->feat_flags |= BIT(id);
  } else {
    BRCMF_DBG(TRACE, "%s feature check failed: %s, fw err %s", brcmf_feat_names[id],
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
  }
}

/**
 * brcmf_feat_iovar_data_get() - determine feature through iovar data query.
 *
 * @ifp: interface to query.
 * @id: feature id.
 * @name: iovar name.
 * @len: length (in bytes) of data type for the given iovar.
 */
static void brcmf_feat_iovar_data_get(struct brcmf_if* ifp, enum brcmf_feat_id id, const char* name,
                                      size_t len) {
  void* data = std::malloc(len);
  if (data == nullptr) {
    BRCMF_DBG(TRACE, "%s feature check failed: malloc failed", brcmf_feat_names[id]);
    return;
  }
  const auto status = brcmf_fil_iovar_data_get(ifp, name, data, len, nullptr);
  if (status == ZX_OK) {
    BRCMF_DBG(INFO, "enabling feature: %s", brcmf_feat_names[id]);
    ifp->drvr->feat_flags |= BIT(id);
  } else {
    BRCMF_DBG(TRACE, "%s feature check failed: %d", brcmf_feat_names[id], status);
  }
  std::free(data);
}

static void brcmf_feat_iovar_data_set(struct brcmf_if* ifp, enum brcmf_feat_id id, const char* name,
                                      const void* data, size_t len, bcme_status_t* fwerr_ptr) {
  zx_status_t err;

  err = brcmf_fil_iovar_data_set(ifp, name, data, len, fwerr_ptr);
  if (err == ZX_OK) {
    BRCMF_DBG(INFO, "enabling feature: %s", brcmf_feat_names[id]);
    ifp->drvr->feat_flags |= BIT(id);
  } else if (err == ZX_ERR_NOT_SUPPORTED) {
    // brcmf_fil_iovar_data_set() returns the result of brcmf_fil_cmd_data, which returned
    // -EBADE on any firmware error rather than passing the firmware error through. The
    // original error check was "(err != -BRCMF_FW_UNSUPPORTED)" which meant that if the
    // firmware reported BRCMF_FW_UNSUPPORTED, this logic would see -EBADE and think all
    // was well.
    BRCMF_DBG(INFO, " * * NOT enabling feature %s, though the Linux driver would have",
              brcmf_feat_names[id]);
  } else {
    BRCMF_DBG(TRACE, "%s feature check failed: %d", brcmf_feat_names[id], err);
  }
}

#define MAX_CAPS_BUFFER_SIZE 512
static void brcmf_feat_firmware_capabilities(struct brcmf_if* ifp) {
  char caps[MAX_CAPS_BUFFER_SIZE];
  enum brcmf_feat_id id;
  int i;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  err = brcmf_fil_iovar_data_get(ifp, "cap", caps, sizeof(caps), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to determine firmware capabilities: %s, fw err %s\n",
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    return;
  }

  caps[sizeof(caps) - 1] = 0;
  BRCMF_DBG(INFO, "[ %s]", caps);

  for (i = 0; i < (int)countof(brcmf_fwcap_map); i++) {
    if (strstr(caps, brcmf_fwcap_map[i].fwcap_id)) {
      id = brcmf_fwcap_map[i].feature;
      BRCMF_DBG(INFO, "enabling driver feature: %s", brcmf_feat_names[id]);
      ifp->drvr->feat_flags |= BIT(id);
    }
  }
}

void brcmf_feat_attach(struct brcmf_pub* drvr) {
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, 0);
  struct brcmf_gscan_config gscan_cfg;
  uint32_t wowl_cap;
  zx_status_t err;

  brcmf_feat_firmware_capabilities(ifp);
  memset(&gscan_cfg, 0, sizeof(gscan_cfg));
  if (drvr->bus_if->chip != BRCM_CC_43430_CHIP_ID && drvr->bus_if->chip != BRCM_CC_4345_CHIP_ID)
    brcmf_feat_iovar_data_set(ifp, BRCMF_FEAT_GSCAN, "pfn_gscan_cfg", &gscan_cfg, sizeof(gscan_cfg),
                              nullptr);
  brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_PNO, "pfn");
  if (drvr->bus_if->wowl_supported) {
    brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_WOWL, "wowl");
  }
  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_WOWL)) {
    err = brcmf_fil_iovar_int_get(ifp, "wowl_cap", &wowl_cap, nullptr);
    if (err == ZX_OK) {
      ifp->drvr->feat_flags |= BIT(BRCMF_FEAT_WOWL_ARP_ND);
      if (wowl_cap & BRCMF_WOWL_PFN_FOUND) {
        ifp->drvr->feat_flags |= BIT(BRCMF_FEAT_WOWL_ND);
      }
      if (wowl_cap & BRCMF_WOWL_GTK_FAILURE) {
        ifp->drvr->feat_flags |= BIT(BRCMF_FEAT_WOWL_GTK);
      }
    }
  }
  /* MBSS does not work for 43362 */
  if (drvr->bus_if->chip == BRCM_CC_43362_CHIP_ID) {
    ifp->drvr->feat_flags &= ~BIT(BRCMF_FEAT_MBSS);
  }
  brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_RSDB, "rsdb_mode");
  brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_TDLS, "tdls_enable");
  brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_MFP, "mfp");

  brcmf_feat_iovar_data_get(ifp, BRCMF_FEAT_SCAN_RANDOM_MAC, "pfn_macaddr",
                            sizeof(brcmf_pno_macaddr_le));

  if (drvr->settings->feature_disable) {
    BRCMF_DBG(INFO, "Features: 0x%02x, disable: 0x%02x", ifp->drvr->feat_flags,
              drvr->settings->feature_disable);
    ifp->drvr->feat_flags &= ~drvr->settings->feature_disable;
  }
  brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_FWSUP, "sup_wpa");

  brcmf_feat_iovar_data_get(ifp, BRCMF_FEAT_DHIST, "wstats_counters", sizeof(wl_wstats_cnt_t));

  /* set chip related quirks */
  switch (drvr->bus_if->chip) {
    case BRCM_CC_43236_CHIP_ID:
      drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_AUTO_AUTH);
      break;
    case BRCM_CC_4329_CHIP_ID:
      drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_NEED_MPC);
      break;
    case BRCM_CC_4359_CHIP_ID:
      drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_IS_4359);
      break;
    default:
      /* no quirks */
      break;
  }
}

bool brcmf_feat_is_enabled(brcmf_pub* drvr, enum brcmf_feat_id id) {
  return (drvr->feat_flags & BIT(id));
}

bool brcmf_feat_is_enabled(struct brcmf_if* ifp, enum brcmf_feat_id id) {
  return brcmf_feat_is_enabled(ifp->drvr, id);
}

bool brcmf_feat_is_quirk_enabled(struct brcmf_if* ifp, enum brcmf_feat_quirk quirk) {
  return (ifp->drvr->chip_quirks & BIT(quirk));
}
