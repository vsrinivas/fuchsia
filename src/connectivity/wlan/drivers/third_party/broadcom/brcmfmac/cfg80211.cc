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

/* Toplevel file. Relies on dhd_linux.c to send commands to the dongle. */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h"

#include <fuchsia/hardware/wlan/fullmac/c/banjo.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/cpp/fidl.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/ddk/metadata.h>
#include <lib/zx/clock.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <vector>

#include <wifi/wifi-config.h>
#include <wlan/common/element.h>
#include <wlan/common/ieee80211.h>
#include <wlan/common/ieee80211_codes.h>
#include <wlan/common/macaddr.h>

#include "fuchsia/wlan/ieee80211/c/banjo.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_d11.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_wifi.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/btcoex.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/defs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/feature.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fweh.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/linuxisms.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/proto.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/workqueue.h"
#include "third_party/bcmdhd/crossdriver/dhd.h"
#include "third_party/bcmdhd/crossdriver/include/proto/802.11.h"
#include "third_party/bcmdhd/crossdriver/wlioctl.h"

#define BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS 320
#define BRCMF_SCAN_JOIN_PASSIVE_DWELL_TIME_MS 400
#define BRCMF_SCAN_JOIN_PROBE_INTERVAL_MS 20

#define BRCMF_SCAN_CHANNEL_TIME 40
#define BRCMF_SCAN_UNASSOC_TIME 40
#define BRCMF_SCAN_PASSIVE_TIME 120

#define BRCMF_ND_INFO_TIMEOUT_MSEC 2000
// Wait until disconnect is complete. This is somewhat arbitrary. The disconnect process
// involves issuing a few iovars to FW in addition to notifying SME. But SME sometimes
// attempts to reconnect right away and that might preempt the disconnect.
#define BRCMF_WAIT_FOR_DISCONNECT_MSEC ZX_MSEC(500)
// Rate returned by FW (in units of Mbps) is multiplied by 2 to avoid passing fractional value
#define BRCMF_CONVERT_TO_REAL_RATE(fw_rate) (fw_rate / 2.0)

#define EXEC_TIMEOUT_WORKER(worker)                                       \
  {                                                                       \
    if (brcmf_bus_get_bus_type(cfg->pub->bus_if) == BRCMF_BUS_TYPE_SIM) { \
      (*cfg->worker.handler)(&cfg->worker);                               \
    } else {                                                              \
      WorkQueue::ScheduleDefault(&cfg->worker);                           \
    }                                                                     \
  }

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

static bool check_vif_up(struct brcmf_cfg80211_vif* vif) {
  if (!brcmf_test_bit(brcmf_vif_status_bit_t::READY, &vif->sme_state)) {
    BRCMF_INFO("device is not ready : status (%lu)", vif->sme_state.load());
    return false;
  }
  return true;
}

static uint8_t __wl_rates[] = {
    BRCM_RATE_1M,  BRCM_RATE_2M,  BRCM_RATE_5M5, BRCM_RATE_11M, BRCM_RATE_6M,  BRCM_RATE_9M,
    BRCM_RATE_12M, BRCM_RATE_18M, BRCM_RATE_24M, BRCM_RATE_36M, BRCM_RATE_48M, BRCM_RATE_54M,
};

#define wl_g_rates (__wl_rates + 0)
#define wl_g_rates_size std::size(__wl_rates)
#define wl_a_rates (__wl_rates + 4)
#define wl_a_rates_size ((size_t)(wl_g_rates_size - 4))

/* Vendor specific ie. id = 221, oui and type defines exact ie */
struct brcmf_vs_tlv {
  uint8_t id;
  uint8_t len;
  uint8_t oui[3];
  uint8_t oui_type;
};

struct parsed_vndr_ie_info {
  uint8_t* ie_ptr;
  uint32_t ie_len; /* total length including id & length field */
  struct brcmf_vs_tlv vndrie;
};

struct parsed_vndr_ies {
  uint32_t count;
  struct parsed_vndr_ie_info ie_info[VNDR_IE_PARSE_LIMIT];
};

#define X(SCAN_STATUS)                       \
  case brcmf_scan_status_bit_t::SCAN_STATUS: \
    return #SCAN_STATUS;
const char* brcmf_get_scan_status_bit_str(brcmf_scan_status_bit_t scan_status) {
  switch (scan_status) { BRCMF_SCAN_STATUS_LIST };
}
#undef X

#define X(CONNECT_STATUS)                      \
  case brcmf_connect_status_t::CONNECT_STATUS: \
    return #CONNECT_STATUS;
const char* brcmf_get_connect_status_str(brcmf_connect_status_t connect_status) {
  switch (connect_status) { BRCMF_CONNECT_STATUS_LIST };
}
#undef X

static inline void fill_with_broadcast_addr(uint8_t* address) { memset(address, 0xff, ETH_ALEN); }
static inline void fill_with_wildcard_ssid(struct brcmf_ssid_le* ssid_le) { *ssid_le = {}; }

/* Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
static const struct brcmf_tlv* brcmf_parse_tlvs(const void* buf, int buflen, uint key) {
  const struct brcmf_tlv* elt = static_cast<decltype(elt)>(buf);
  int totlen = buflen;

  /* find tagged parameter */
  while (totlen >= TLV_HDR_LEN) {
    int len = elt->len;

    /* validate remaining totlen */
    if ((elt->id == key) && (totlen >= (len + TLV_HDR_LEN))) {
      return elt;
    }

    elt = (struct brcmf_tlv*)((uint8_t*)elt + (len + TLV_HDR_LEN));
    totlen -= (len + TLV_HDR_LEN);
  }

  return nullptr;
}

static zx_status_t brcmf_vif_change_validate(struct brcmf_cfg80211_info* cfg,
                                             struct brcmf_cfg80211_vif* vif, uint16_t new_type) {
  struct brcmf_cfg80211_vif* pos;
  bool check_combos = false;
  zx_status_t ret = ZX_OK;
  struct iface_combination_params params = {
      .num_different_channels = 1,
  };

  list_for_every_entry (&cfg->vif_list, pos, struct brcmf_cfg80211_vif, list) {
    if (pos == vif) {
      params.iftype_num[new_type]++;
    } else {
      /* concurrent interfaces so need check combinations */
      check_combos = true;
      params.iftype_num[pos->wdev.iftype]++;
    }
  }

  if (check_combos) {
    ret = cfg80211_check_combinations(cfg, &params);
  }

  return ret;
}

static zx_status_t brcmf_vif_add_validate(struct brcmf_cfg80211_info* cfg,
                                          wlan_mac_role_t new_type) {
  struct brcmf_cfg80211_vif* pos;
  struct iface_combination_params params = {
      .num_different_channels = 1,
  };

  list_for_every_entry (&cfg->vif_list, pos, struct brcmf_cfg80211_vif, list) {
    params.iftype_num[pos->wdev.iftype]++;
  }

  params.iftype_num[new_type]++;
  return cfg80211_check_combinations(cfg, &params);
}

static void convert_key_from_CPU(struct brcmf_wsec_key* key, struct brcmf_wsec_key_le* key_le) {
  key_le->index = key->index;
  key_le->len = key->len;
  key_le->algo = key->algo;
  key_le->flags = key->flags;
  key_le->rxiv.hi = key->rxiv.hi;
  key_le->rxiv.lo = key->rxiv.lo;
  key_le->iv_initialized = key->iv_initialized;
  memcpy(key_le->data, key->data, sizeof(key->data));
  memcpy(key_le->ea, key->ea, sizeof(key->ea));
}

static zx_status_t send_key_to_dongle(struct brcmf_if* ifp, struct brcmf_wsec_key* key) {
  zx_status_t err;
  struct brcmf_wsec_key_le key_le;

  convert_key_from_CPU(key, &key_le);

  brcmf_netdev_wait_pend8021x(ifp);

  err = brcmf_fil_bsscfg_data_set(ifp, "wsec_key", &key_le, sizeof(key_le));

  if (err != ZX_OK) {
    BRCMF_ERR("wsec_key error (%d)", err);
  }
  return err;
}

static void brcmf_cfg80211_update_proto_addr_mode(struct wireless_dev* wdev) {
  struct brcmf_cfg80211_vif* vif;
  struct brcmf_if* ifp;

  vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
  ifp = vif->ifp;

  if (wdev->iftype == WLAN_MAC_ROLE_AP) {
    brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx, ADDR_DIRECT);
  } else {
    brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx, ADDR_INDIRECT);
  }
}

static int32_t brcmf_get_first_free_bsscfgidx(struct brcmf_pub* drvr) {
  int bsscfgidx;

  for (bsscfgidx = 0; bsscfgidx < BRCMF_MAX_IFS; bsscfgidx++) {
    /* bsscfgidx 1 is reserved for legacy P2P */
    if (bsscfgidx == 1) {
      continue;
    }
    if (!drvr->iflist[bsscfgidx]) {
      return bsscfgidx;
    }
  }

  return -1;
}

static int32_t brcmf_get_prealloced_bsscfgidx(struct brcmf_pub* drvr) {
  int bsscfgidx;
  net_device* ndev;

  for (bsscfgidx = 0; bsscfgidx < BRCMF_MAX_IFS; bsscfgidx++) {
    /* bsscfgidx 1 is reserved for legacy P2P */
    if (bsscfgidx == 1) {
      continue;
    }
    if (drvr->iflist[bsscfgidx]) {
      ndev = drvr->iflist[bsscfgidx]->ndev;
      if (ndev && ndev->needs_free_net_device) {
        return bsscfgidx;
      }
    }
  }

  return -1;
}

static zx_status_t brcmf_cfg80211_request_ap_if(struct brcmf_if* ifp) {
  struct brcmf_mbss_ssid_le mbss_ssid_le;
  int bsscfgidx;
  zx_status_t err;

  memset(&mbss_ssid_le, 0, sizeof(mbss_ssid_le));
  bsscfgidx = brcmf_get_first_free_bsscfgidx(ifp->drvr);
  if (bsscfgidx < 0) {
    return ZX_ERR_NO_MEMORY;
  }

  mbss_ssid_le.bsscfgidx = bsscfgidx;
  mbss_ssid_le.SSID_len = 5;
  sprintf((char*)mbss_ssid_le.SSID, "ssid%d", bsscfgidx);

  err = brcmf_fil_bsscfg_data_set(ifp, "bsscfg:ssid", &mbss_ssid_le, sizeof(mbss_ssid_le));
  if (err != ZX_OK) {
    BRCMF_ERR("setting ssid failed %d", err);
  }

  return err;
}

/*For now this function should always be called when adding iface*/
static zx_status_t brcmf_set_iface_macaddr(net_device* ndev,
                                           const wlan::common::MacAddr& mac_addr) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  bcme_status_t fw_err = BCME_OK;
  zx_status_t err = ZX_OK;

  BRCMF_DBG(TRACE, "Enter");
  // If the existing mac_addr of this iface is the same as it is, just return success.
  if (!memcmp(ifp->mac_addr, mac_addr.byte, ETH_ALEN)) {
    return ZX_OK;
  }

  err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", mac_addr.byte, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting mac address failed: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    return err;
  }

  BRCMF_INFO("Setting mac address of ndev:%s.", ifp->ndev->name);
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(mac_addr.byte));
#endif /* !defined(NDEBUG) */

  memcpy(ifp->mac_addr, mac_addr.byte, sizeof(ifp->mac_addr));

  return err;
}

// Derive the mac address for the SoftAP interface from the system mac address
// (which is used for the client interface).
zx_status_t brcmf_gen_ap_macaddr(struct brcmf_if* ifp, wlan::common::MacAddr& out_mac_addr) {
  bcme_status_t fw_err = BCME_OK;
  uint8_t gen_mac_addr[ETH_ALEN];

  zx_status_t err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", gen_mac_addr, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Retrieving mac address from firmware failed: %s, fw err %s",
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    return err;
  }

  // Modify the mac address as follows:
  // Mark the address as unicast and locally administered. In addition, modify
  // byte 5 (increment) to ensure that it is different from the original address
  gen_mac_addr[0] &= 0xfe;  // bit 0: 0 = unicast
  gen_mac_addr[0] |= 0x02;  // bit 1: 1 = locally-administered
  gen_mac_addr[5]++;

  out_mac_addr.Set(gen_mac_addr);
  return ZX_OK;
}

static zx_status_t brcmf_set_ap_macaddr(struct brcmf_if* ifp,
                                        const std::optional<wlan::common::MacAddr>& in_mac_addr) {
  wlan::common::MacAddr mac_addr;
  zx_status_t err = ZX_OK;

  // Use the provided mac_addr if it passed.
  if (in_mac_addr) {
    mac_addr = *in_mac_addr;
  } else {
    // If MAC address is not provided, we generate one using the current MAC address.
    // By default it is derived from the system mac address set during init.
    err = brcmf_gen_ap_macaddr(ifp, mac_addr);
    if (err != ZX_OK) {
      BRCMF_ERR("Failed to generate MAC address for AP iface netdev: %s", ifp->ndev->name);
      return err;
    }
  }

  err = brcmf_set_iface_macaddr(ifp->ndev, mac_addr);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to set MAC address for AP iface netdev: %s.", ifp->ndev->name);
#if !defined(NDEBUG)
    BRCMF_DBG(WLANIF, "  address: " FMT_MAC, FMT_MAC_ARGS(mac_addr.byte));
#endif /* !defined(NDEBUG) */

    return err;
  }

  return ZX_OK;
}

static zx_status_t brcmf_cfg80211_change_iface_mac_role(struct brcmf_cfg80211_info* cfg,
                                                        struct net_device* ndev,
                                                        wlan_mac_role_t mac_role) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_vif* vif = ifp->vif;
  const char* mac_role_str = "OTHER";
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_DBG(TRACE, "Enter");

  err = brcmf_vif_change_validate(cfg, vif, mac_role);
  if (err != ZX_OK) {
    BRCMF_ERR("iface validation failed: err=%d", err);
    return err;
  }
  switch (mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
      err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 1, &fw_err);
      if (err != ZX_OK) {
        BRCMF_ERR("WLC_SET_INFRA error: %s, fw err %s", zx_status_get_string(err),
                  brcmf_fil_get_errstr(fw_err));
        err = ZX_ERR_UNAVAILABLE;
        goto done;
      }
      mac_role_str = const_cast<char*>("CLIENT");
      break;
    case WLAN_MAC_ROLE_AP:
      mac_role_str = const_cast<char*>("AP");
      break;
    default:
      err = ZX_ERR_OUT_OF_RANGE;
      goto done;
  }

  vif->wdev.iftype = mac_role;
  brcmf_cfg80211_update_proto_addr_mode(&vif->wdev);
  BRCMF_IFDBG(WLANIF, ndev, "iface mac role set to \"%s\"", mac_role_str);

done:
  BRCMF_DBG(TRACE, "Exit");

  return err;
}

/**
 * brcmf_ap_add_vif() - create a new AP virtual interface for multiple BSS
 *
 * @cfg: config of new interface.
 * @name: name of the new interface.
 * @dev_out: address of wireless dev pointer
 */
static zx_status_t brcmf_ap_add_vif(struct brcmf_cfg80211_info* cfg, const char* name,
                                    const std::optional<wlan::common::MacAddr>& mac_addr,
                                    struct wireless_dev** dev_out) {
  struct brcmf_if* ifp = cfg_to_if(cfg);
  struct brcmf_cfg80211_vif* vif;
  zx_status_t err;

  // We need to create the SoftAP IF if we are not operating with manufacturing FW.
  if (!brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    if (brcmf_cfg80211_vif_event_armed(cfg)) {
      return ZX_ERR_UNAVAILABLE;
    }

    BRCMF_INFO("Adding vif \"%s\"", name);

    err = brcmf_alloc_vif(cfg, WLAN_MAC_ROLE_AP, &vif);
    if (err != ZX_OK) {
      if (dev_out) {
        *dev_out = nullptr;
      }
      return err;
    }

    brcmf_cfg80211_arm_vif_event(cfg, vif, BRCMF_E_IF_ADD);

    err = brcmf_cfg80211_request_ap_if(ifp);
    if (err != ZX_OK) {
      brcmf_cfg80211_disarm_vif_event(cfg);
      goto fail;
    }
    /* wait for firmware event */
    err = brcmf_cfg80211_wait_vif_event(cfg, ZX_MSEC(BRCMF_VIF_EVENT_TIMEOUT_MSEC));
    brcmf_cfg80211_disarm_vif_event(cfg);
    if (err != ZX_OK) {
      BRCMF_ERR("timeout occurred");
      err = ZX_ERR_IO;
      goto fail;
    }
  } else {
    // Else reuse the existing IF itself but change its type
    vif = ifp->vif;
    vif->ifp = ifp;
    err = brcmf_cfg80211_change_iface_mac_role(cfg, ifp->ndev, WLAN_MAC_ROLE_AP);
    if (err != ZX_OK) {
      BRCMF_ERR("failed to change iface mac role to AP: %s", zx_status_get_string(err));
      err = ZX_ERR_IO;
      goto fail;
    }
  }

  /* interface created in firmware */
  ifp = vif->ifp;
  if (!ifp) {
    BRCMF_ERR("no if pointer provided");
    err = ZX_ERR_INVALID_ARGS;
    goto fail;
  }

  strncpy(ifp->ndev->name, name, sizeof(ifp->ndev->name) - 1);
  err = brcmf_net_attach(ifp, true);
  if (err != ZX_OK) {
    BRCMF_ERR("Registering netdevice failed");
    brcmf_free_net_device(ifp->ndev);
    goto fail;
  }

  err = brcmf_set_ap_macaddr(ifp, mac_addr);
  if (err != ZX_OK) {
    BRCMF_ERR("unable to set mac address of ap if");
    goto fail;
  }

  if (dev_out) {
    *dev_out = &ifp->vif->wdev;
  }
  return ZX_OK;

fail:
  brcmf_free_vif(vif);
  if (dev_out) {
    *dev_out = nullptr;
  }
  return err;
}

static bool brcmf_is_apmode(struct brcmf_cfg80211_vif* vif) {
  uint16_t iftype;

  iftype = vif->wdev.iftype;
  return iftype == WLAN_MAC_ROLE_AP;
}

static bool brcmf_is_existing_macaddr(brcmf_pub* drvr, const uint8_t mac_addr[ETH_ALEN],
                                      bool is_ap) {
  if (is_ap) {
    for (const auto& iface : drvr->iflist) {
      if (iface != nullptr && !memcmp(iface->mac_addr, mac_addr, ETH_ALEN)) {
        return true;
      }
    }
  } else {
    for (const auto& iface : drvr->iflist) {
      if (iface != nullptr && iface->vif->wdev.iftype != WLAN_MAC_ROLE_CLIENT &&
          !memcmp(iface->mac_addr, mac_addr, ETH_ALEN)) {
        return true;
      }
    }
  }
  return false;
}

zx_status_t brcmf_cfg80211_add_iface(brcmf_pub* drvr, const char* name, struct vif_params* params,
                                     const wlanphy_impl_create_iface_req_t* req,
                                     struct wireless_dev** wdev_out) {
  zx_status_t err;
  net_device* ndev;
  wireless_dev* wdev;
  int32_t bsscfgidx;

  BRCMF_DBG(TRACE, "enter: %s type %d", name, req->role);

  if (wdev_out == nullptr) {
    BRCMF_ERR("cannot write wdev to nullptr");
    return ZX_ERR_INVALID_ARGS;
  }

  err = brcmf_vif_add_validate(drvr->config, req->role);
  if (err != ZX_OK) {
    BRCMF_ERR("iface validation failed: err=%d", err);
    return err;
  }

  struct brcmf_if* ifp;
  const char* iface_role_name;

  std::optional<wlan::common::MacAddr> mac_addr;
  if (req->has_init_sta_addr) {
    mac_addr.emplace(req->init_sta_addr);
  }

  switch (req->role) {
    case WLAN_MAC_ROLE_AP:
      iface_role_name = "ap";

      if (mac_addr && brcmf_is_existing_macaddr(drvr, mac_addr->byte, true)) {
        return ZX_ERR_ALREADY_EXISTS;
      }

      err = brcmf_ap_add_vif(drvr->config, name, mac_addr, &wdev);
      if (err != ZX_OK) {
        BRCMF_ERR("add iface %s type %d failed: err=%d", name, req->role, err);
        return err;
      }

      brcmf_cfg80211_update_proto_addr_mode(wdev);
      ndev = wdev->netdev;
      wdev->iftype = req->role;
      ndev->mlme_channel = zx::channel(req->mlme_channel);

      break;
    case WLAN_MAC_ROLE_CLIENT: {
      iface_role_name = "client";

      if (mac_addr && brcmf_is_existing_macaddr(drvr, mac_addr->byte, false)) {
        return ZX_ERR_ALREADY_EXISTS;
      }
      bsscfgidx = brcmf_get_prealloced_bsscfgidx(drvr);
      if (bsscfgidx < 0) {
        return ZX_ERR_NO_MEMORY;
      }

      ndev = drvr->iflist[bsscfgidx]->ndev;
      if (strncmp(ndev->name, name, sizeof(ndev->name))) {
        BRCMF_INFO("Reusing netdev:%s for new client iface, but changing its name to netdev:%s.",
                   ndev->name, name);
        brcmf_write_net_device_name(ndev, name);
      }
      ifp = brcmf_get_ifp(drvr, 0);

      if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
        // Since a single IF is shared when operating with manufacturing FW, change
        // IF type.
        err = brcmf_cfg80211_change_iface_mac_role(drvr->config, ifp->ndev, WLAN_MAC_ROLE_CLIENT);
        if (err != ZX_OK) {
          BRCMF_ERR("failed to change iface mac role to CLIENT: %s", zx_status_get_string(err));
          return err;
        }
      }
      wdev = &drvr->iflist[bsscfgidx]->vif->wdev;
      wdev->iftype = req->role;
      ndev->mlme_channel = zx::channel(req->mlme_channel);
      ndev->needs_free_net_device = false;

      // Use input mac_addr if it's provided. Otherwise, fallback to the bootloader
      // MAC address. Note that this fallback MAC address is intended for client ifaces only.
      wlan::common::MacAddr client_mac_addr;
      if (mac_addr) {
        client_mac_addr = *mac_addr;
      } else {
        err = brcmf_bus_get_bootloader_macaddr(drvr->bus_if, client_mac_addr.byte);
        if (err != ZX_OK || client_mac_addr.IsZero() || client_mac_addr.IsBcast()) {
          if (err != ZX_OK) {
            BRCMF_ERR("Failed to get valid mac address from bootloader: %s",
                      zx_status_get_string(err));
          } else {
            BRCMF_ERR("Failed to get valid mac address from bootloader.");
#if !defined(NDEBUG)
            BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(client_mac_addr.byte));
#endif /* !defined(NDEBUG) */
          }
          err = brcmf_gen_random_mac_addr(client_mac_addr.byte);
          if (err != ZX_OK) {
            BRCMF_ERR("Failed to generate random MAC address.");
            return err;
          }
          BRCMF_ERR("Falling back to random mac address");
#if !defined(NDEBUG)
          BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(client_mac_addr.byte));
#endif /* !defined(NDEBUG) */
        } else {
          BRCMF_IFDBG(WLANIF, ndev, "Retrieved bootloader wifi MAC addresss");
#if !defined(NDEBUG)
          BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(client_mac_addr.byte));
#endif /* !defined(NDEBUG) */
        }
      }

      err = brcmf_set_iface_macaddr(ndev, client_mac_addr);
      if (err != ZX_OK) {
        BRCMF_ERR("Failed to set MAC address for client iface netdev:%s", ndev->name);
#if !defined(NDEBUG)
        BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(client_mac_addr.byte));
#endif /* !defined(NDEBUG) */
        return err;
      }

      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  *wdev_out = wdev;
  return ZX_OK;
}

static void brcmf_scan_config_mpc(struct brcmf_if* ifp, int mpc) {
  if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_NEED_MPC)) {
    brcmf_enable_mpc(ifp, mpc);
  }
}

// This function set "mpc" to the requested value only if SoftAP
// has not been started. Else it sets "mpc" to 0.
void brcmf_enable_mpc(struct brcmf_if* ifp, int mpc) {
  BRCMF_DBG(TRACE, "Enter");

  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  // If AP has been started, mpc is always 0
  if (cfg->ap_started) {
    mpc = 0;
  }
  err = brcmf_fil_iovar_int_set(ifp, "mpc", mpc, &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("fail to set mpc: %s, fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
    return;
  }
  BRCMF_DBG(TRACE, "Exit");
}

static void brcmf_signal_scan_end(struct net_device* ndev, uint64_t txn_id,
                                  uint8_t scan_result_code) {
  wlan_fullmac_scan_end_t args;
  args.txn_id = txn_id;
  args.code = scan_result_code;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped-- skipping signal scan end callback ");
  } else {
    BRCMF_DBG(SCAN, "Signaling on_scan_end with txn_id %ld and code %d", args.txn_id, args.code);
    BRCMF_IFDBG(WLANIF, ndev,
                "Sending scan end event to SME. txn_id: %" PRIu64
                ", result: %s"
                ", number of results: %" PRIu32 "",
                args.txn_id,
                args.code == WLAN_SCAN_RESULT_SUCCESS          ? "success"
                : args.code == WLAN_SCAN_RESULT_NOT_SUPPORTED  ? "not supported"
                : args.code == WLAN_SCAN_RESULT_INVALID_ARGS   ? "invalid args"
                : args.code == WLAN_SCAN_RESULT_INTERNAL_ERROR ? "internal error"
                                                               : "unknown",
                ndev->scan_num_results);
    wlan_fullmac_impl_ifc_on_scan_end(&ndev->if_proto, &args);
  }
}

static zx_status_t brcmf_abort_escan(struct brcmf_if* ifp) {
  /* Do a scan abort to stop the driver's scan engine */
  BRCMF_DBG(SCAN, "ABORT scan in firmware");
  struct brcmf_scan_params_le params_le = {};
  fill_with_broadcast_addr(params_le.bssid);
  params_le.bss_type = DOT11_BSSTYPE_ANY;
  params_le.scan_type = 0;
  params_le.channel_num = 1;
  params_le.nprobes = 1;
  params_le.active_time = -1;
  params_le.passive_time = -1;
  params_le.home_time = -1;
  /* Scan is aborted by setting channel_list[0] to -1 */
  params_le.channel_list[0] = -1;
  /* E-Scan (or anyother type) can be aborted by SCAN */
  bcme_status_t fwerr = BCME_OK;
  zx_status_t err =
      brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCAN, &params_le, sizeof(params_le), &fwerr);
  if (err != ZX_OK) {
    BRCMF_ERR("Scan abort failed: %s (fw err %s)", zx_status_get_string(err),
              brcmf_fil_get_errstr(fwerr));
  }

  return err;
}

static void brcmf_notify_escan_complete(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                                        brcmf_fweh_event_status_t status) {
  BRCMF_DBG(SCAN, "Enter");

  struct net_device* ndev = cfg_to_ndev(cfg);
  uint8_t scan_result = WLAN_SCAN_RESULT_SUCCESS;

  if (!ndev) {
    BRCMF_WARN("Device does not exist, skipping escan complete notify.");
    return;
  }

  // Canceling if it's inactive is OK. Checking if it's active just invites race conditions.
  cfg->escan_timer->Stop();
  brcmf_scan_config_mpc(ifp, 1);

  if (cfg->scan_in_progress) {
    BRCMF_IFDBG(WLANIF, ndev, "ESCAN Completed scan: %s",
                status == BRCMF_E_STATUS_SUCCESS ? "Done"
                : status == BRCMF_E_STATUS_ABORT ? "Aborted"
                                                 : "Errored");
    cfg->scan_in_progress = false;

    switch (status) {
      case BRCMF_E_STATUS_SUCCESS:
        scan_result = WLAN_SCAN_RESULT_SUCCESS;
        break;
      case BRCMF_E_STATUS_NEWASSOC:
        // In this case, the scan process has been interrupted by an assoc inside the firwmare.
      case BRCMF_E_STATUS_ABORT:
        BRCMF_INFO("Sending notification of aborted scan: %d", status);
        scan_result = WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE;
        break;
      default:
        BRCMF_WARN("Sending notification of failed scan: %d", status);
        scan_result = WLAN_SCAN_RESULT_INTERNAL_ERROR;
    }
    brcmf_signal_scan_end(ndev, ndev->scan_txn_id, scan_result);
  }

  if (!brcmf_test_and_clear_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status)) {
    BRCMF_DBG(SCAN, "Scan complete, probably P2P scan");
  }
}

static zx_status_t brcmf_cfg80211_del_ap_iface(struct brcmf_cfg80211_info* cfg,
                                               struct wireless_dev* wdev) {
  struct net_device* ndev = wdev->netdev;
  struct brcmf_if* ifp = nullptr;
  zx_status_t err = ZX_OK;

  if (ndev)
    ifp = ndev_to_if(ndev);
  else {
    BRCMF_ERR("Net device is nullptr");
    return ZX_ERR_IO;
  }

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    // If we are operating with manufacturing FW, we just have a single IF. Pretend like it was
    // deleted.
    return ZX_OK;
  }

  // If we are in the process of resetting, then ap interface no longer exists
  // in firmware (since fw has been reloaded). We can skip sending commands
  // related to destorying the interface.
  if (ifp->drvr->drvr_resetting.load()) {
    goto skip_fw_cmds;
  }

  brcmf_cfg80211_arm_vif_event(cfg, ifp->vif, BRCMF_E_IF_DEL);

  err = brcmf_fil_bsscfg_data_set(ifp, "interface_remove", nullptr, 0);
  if (err != ZX_OK) {
    BRCMF_ERR("interface_remove interface %d failed %d", ifp->ifidx, err);
    brcmf_cfg80211_disarm_vif_event(cfg);
    return err;
  }

  /* wait for firmware event */
  err = brcmf_cfg80211_wait_vif_event(cfg, ZX_MSEC(BRCMF_VIF_EVENT_TIMEOUT_MSEC));
  if (err != ZX_OK) {
    BRCMF_ERR("BRCMF_VIF_EVENT timeout occurred");
    brcmf_cfg80211_disarm_vif_event(cfg);
    return ZX_ERR_IO;
  }
  brcmf_cfg80211_disarm_vif_event(cfg);

skip_fw_cmds:
  brcmf_remove_interface(ifp, true);
  return err;
}

static zx_status_t brcmf_dev_escan_set_randmac(struct brcmf_if* ifp) {
  struct brcmf_pno_macaddr_le pfn_mac = {};
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  pfn_mac.version = BRCMF_PFN_MACADDR_CFG_VER;
  pfn_mac.flags = BRCMF_PFN_USE_FULL_MACADDR;

  err = brcmf_gen_random_mac_addr(pfn_mac.mac);
  if (err != ZX_OK) {
    return err;
  }

  err = brcmf_fil_iovar_data_set(ifp, "pfn_macaddr", &pfn_mac, sizeof(pfn_mac), &fw_err);
  if (err)
    BRCMF_ERR("set escan randmac failed, err=%d, fw_err=%d", err, fw_err);

  return err;
}

static zx_status_t brcmf_escan_prep(struct brcmf_cfg80211_info* cfg,
                                    struct brcmf_scan_params_le* params_le,
                                    const wlan_fullmac_scan_req_t* request) {
  uint32_t n_ssids;
  uint32_t n_channels;
  int32_t offset;
  uint16_t chanspec;

  fill_with_broadcast_addr(params_le->bssid);

  /* Wildcard SSID serves as a fallback value if ssids_list is empty. Otherwise,
     this field is ignored. */
  fill_with_wildcard_ssid(&params_le->ssid_le);

  // Do not filter scan results based on BSS type.
  params_le->bss_type = DOT11_BSSTYPE_ANY;

  if (request->scan_type == WLAN_SCAN_TYPE_ACTIVE) {
    params_le->scan_type = BRCMF_SCANTYPE_ACTIVE;
    params_le->active_time = request->min_channel_time;
    params_le->nprobes = BRCMF_ACTIVE_SCAN_NUM_PROBES;
    params_le->passive_time = -1;
  } else {
    params_le->scan_type = BRCMF_SCANTYPE_PASSIVE;
    params_le->passive_time = request->min_channel_time;
    params_le->active_time = -1;
  }
  params_le->channel_num = 0;
  params_le->home_time = -1;

  /* Copy channel array if applicable */
  n_channels = request->channels_count;
  BRCMF_DBG(SCAN, "### List of channelspecs to scan ### %d", n_channels);
  if (n_channels > 0) {
    for (uint32_t i = 0; i < n_channels; i++) {
      wlan_channel_t wlan_chan;
      wlan_chan.primary = request->channels_list[i];
      wlan_chan.cbw = CHANNEL_BANDWIDTH_CBW20;
      wlan_chan.secondary80 = 0;
      chanspec = channel_to_chanspec(&cfg->d11inf, &wlan_chan);
      BRCMF_DBG(SCAN, "Chan : %d, Channel spec: %x", request->channels_list[i], chanspec);
      params_le->channel_list[i] = chanspec;
    }
  } else {
    BRCMF_ERR("Scan request contains empty channel list.");
    return ZX_ERR_INVALID_ARGS;
  }

  /* Add number of channels to channel_num */
  params_le->channel_num = n_channels & BRCMF_SCAN_PARAMS_COUNT_MASK;

  /* Set SSID fields as applicable */
  n_ssids = request->ssids_count;
  BRCMF_DBG(SCAN, "### List of SSIDs to scan ### %d", n_ssids);

  /* Copy ssids_list if non-empty */
  if (n_ssids > 0) {
    offset = offsetof(struct brcmf_scan_params_le, channel_list) + n_channels * sizeof(uint16_t);
    offset = roundup(offset, sizeof(uint32_t));
    struct brcmf_ssid_le* ssid_le =
        reinterpret_cast<struct brcmf_ssid_le*>(reinterpret_cast<char*>(params_le) + offset);
    for (uint32_t i = 0; i < n_ssids; i++, ssid_le++) {
      if (request->ssids_list[i].len > wlan_ieee80211::MAX_SSID_BYTE_LEN) {
        BRCMF_ERR("SSID in scan request SSID list too long(no longer than %hhu bytes)",
                  wlan_ieee80211::MAX_SSID_BYTE_LEN);
        return ZX_ERR_INVALID_ARGS;
      }
      ssid_le->SSID_len = request->ssids_list[i].len;
      memcpy(&ssid_le->SSID, request->ssids_list[i].data, request->ssids_list[i].len);
      if (ssid_le->SSID_len == 0) {
        BRCMF_DBG(SCAN, "%d: Broadcast scan", i);
      } else {
        BRCMF_DBG(SCAN, "%d: Targeted scan", i);
#if !defined(NDEBUG)
        BRCMF_DBG(SCAN, "  ssid:" FMT_SSID, FMT_SSID_BYTES(ssid_le->SSID, ssid_le->SSID_len));
#endif /* !defined(NDEBUG) */
      }
    }
  }

  /* Add number of SSIDs to channel_num. See comment at channel_num field declaration. */
  params_le->channel_num |= n_ssids << BRCMF_SCAN_PARAMS_NSSID_SHIFT;

  return ZX_OK;
}

// Calculate the amount of memory needed to hold the escan parameters for a firmware request
static size_t brcmf_escan_params_size(size_t num_channels, size_t num_ssids) {
  size_t size = BRCMF_SCAN_PARAMS_FIXED_SIZE;

  // escan params headers
  size += offsetof(struct brcmf_escan_params_le, params_le);

  // Channel specs
  size += sizeof(uint32_t) * ((num_channels + 1) / 2);

  // SSIDs
  size += sizeof(struct brcmf_ssid_le) * num_ssids;

  return size;
}

static inline uint16_t brcmf_next_sync_id(struct brcmf_cfg80211_info* cfg) {
  return cfg->next_sync_id++;
}

static zx_status_t brcmf_run_escan(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                                   const wlan_fullmac_scan_req_t* request, uint16_t* sync_id_out) {
  if (request == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Validate dwell times
  if (request->min_channel_time == 0 || request->max_channel_time < request->min_channel_time) {
    BRCMF_ERR("Invalid dwell times in escan request min: %u max: %u", request->min_channel_time,
              request->max_channel_time);
    return ZX_ERR_INVALID_ARGS;
  }

  // Calculate space needed for parameters
  size_t params_size = brcmf_escan_params_size(request->channels_count, request->ssids_count);

  // Validate command size
  size_t total_cmd_size = params_size + sizeof("escan");
  // TODO(fxbug.dev/89549): This check seems to be roughly 32 bytes too long, but in practice
  // we never expect to hit the limit.
  if (total_cmd_size >= BRCMF_DCMD_MEDLEN) {
    BRCMF_ERR("Escan params size (%zu) exceeds command max capacity (%d)", total_cmd_size,
              BRCMF_DCMD_MEDLEN);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_DBG(SCAN, "E-SCAN START");

  struct brcmf_escan_params_le* params = static_cast<decltype(params)>(calloc(1, params_size));
  if (!params) {
    err = ZX_ERR_NO_MEMORY;
    goto exit;
  }
  err = brcmf_escan_prep(cfg, &params->params_le, request);
  if (err != ZX_OK) {
    BRCMF_ERR("escan preparation failed");
    goto exit;
  }
  params->version = BRCMF_ESCAN_REQ_VERSION;
  params->action = WL_ESCAN_ACTION_START;
  params->sync_id = brcmf_next_sync_id(cfg);

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_SCAN_RANDOM_MAC) &&
      (params->params_le.scan_type == BRCMF_SCANTYPE_ACTIVE) &&
      !brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
    if ((err = brcmf_dev_escan_set_randmac(ifp)) != ZX_OK) {
      BRCMF_ERR("Failed to set random mac for active scan (%s), using interface mac",
                zx_status_get_string(err));
    }
  }

  err = brcmf_fil_iovar_data_set(ifp, "escan", params, params_size, &fw_err);
  if (err == ZX_OK) {
    *sync_id_out = params->sync_id;
  } else {
    if (err == ZX_ERR_UNAVAILABLE) {
      BRCMF_ERR("system busy : escan canceled sme state: 0x%lx", atomic_load(&ifp->vif->sme_state));
    } else if (err == ZX_ERR_SHOULD_WAIT) {
      BRCMF_INFO("firmware is busy, failing the scan, please retry later. %s, fw err %s",
                 zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    } else {
      BRCMF_ERR("escan failed: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
    }
  }

exit:
  free(params);
  return err;
}

static zx_status_t brcmf_do_escan(struct brcmf_if* ifp, const wlan_fullmac_scan_req_t* req,
                                  uint16_t* sync_id_out) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  zx_status_t err;
  struct escan_info* escan = &cfg->escan_info;

  BRCMF_DBG(SCAN, "Enter");
  escan->ifp = ifp;
  escan->escan_state = WL_ESCAN_STATE_SCANNING;

  brcmf_scan_config_mpc(ifp, 0);

  err = escan->run(cfg, ifp, req, sync_id_out);
  if (err != ZX_OK) {
    brcmf_scan_config_mpc(ifp, 1);
  }
  return err;
}

zx_status_t brcmf_check_scan_status(unsigned long scan_status,
                                    std::string* out_scan_status_report) {
  zx_status_t out_scan_status = ZX_OK;
  std::ostringstream scan_status_ss;

  for (auto scan_status_bit : BRCMF_ALL_SCAN_STATUS_BITS) {
    if (brcmf_test_bit(scan_status_bit, scan_status)) {
      out_scan_status = ZX_ERR_UNAVAILABLE;
      if (out_scan_status_report == nullptr) {
        return out_scan_status;
      }
      if (scan_status_ss.tellp() > 0) {
        scan_status_ss << "+";
      }
      scan_status_ss << brcmf_get_scan_status_bit_str(scan_status_bit);
    }
  }

  if (out_scan_status_report != nullptr) {
    if (scan_status_ss.tellp() > 0) {
      scan_status_ss << " ";
    }
    scan_status_ss << "(0x" << std::hex << scan_status << ")";
    *out_scan_status_report = scan_status_ss.str();
  }
  return out_scan_status;
}

zx_status_t brcmf_cfg80211_scan(struct net_device* ndev, const wlan_fullmac_scan_req_t* req,
                                uint16_t* sync_id_out) {
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter");
  struct wireless_dev* wdev = ndev_to_wdev(ndev);
  struct brcmf_cfg80211_vif* vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
  if (!check_vif_up(vif)) {
    return ZX_ERR_IO;
  }

  struct brcmf_cfg80211_info* cfg = ndev_to_if(ndev)->drvr->config;

  std::string scan_status_report;
  zx_status_t scan_status = brcmf_check_scan_status(cfg->scan_status.load(), &scan_status_report);
  if (scan_status != ZX_OK) {
    BRCMF_ERR("Scanning unavailable: scan_status %s", scan_status_report.c_str());
    return scan_status;
  }

  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &vif->sme_state)) {
    BRCMF_INFO("Scan request suppressed: connect in progress (status: %lu)", vif->sme_state.load());
    return ZX_ERR_SHOULD_WAIT;
  }
  if (brcmf_is_ap_start_pending(cfg)) {
    BRCMF_INFO("AP start request in progress, rejecting scan request, a retry is expected.");
    return ZX_ERR_SHOULD_WAIT;
  }

  BRCMF_DBG(SCAN, "START ESCAN\n");

  cfg->scan_in_progress = true;
  brcmf_set_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status);

  cfg->escan_info.run = brcmf_run_escan;

  err = brcmf_do_escan(vif->ifp, req, sync_id_out);
  if (err != ZX_OK) {
    goto scan_out;
  }

  /* Arm scan timeout timer */
  cfg->escan_timer->Start(ZX_MSEC(BRCMF_ESCAN_TIMER_INTERVAL_MS));
  return ZX_OK;

scan_out:
  if (err != ZX_ERR_SHOULD_WAIT) {
    BRCMF_ERR("scan error (%d)", err);
  }
  brcmf_clear_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status);
  cfg->scan_in_progress = false;
  return err;
}

static void brcmf_init_prof(struct brcmf_cfg80211_profile* prof) { memset(prof, 0, sizeof(*prof)); }

static void brcmf_clear_profile_on_client_disconnect(struct brcmf_cfg80211_profile* prof) {
  // Bssid needs to be preserved for disconnects due to disassoc ind. SME will
  // skip the join and auth steps, and so this will not get repopulated.
  uint8_t bssid[ETH_ALEN];

  memcpy(bssid, prof->bssid, ETH_ALEN);
  brcmf_init_prof(prof);
  memcpy(prof->bssid, bssid, ETH_ALEN);
}

static zx_status_t brcmf_set_pmk(struct brcmf_if* ifp, const uint8_t* pmk_data, uint16_t pmk_len) {
  struct brcmf_wsec_pmk_le pmk;
  int i;
  zx_status_t err;

  /* convert to firmware key format */
  pmk.key_len = pmk_len << 1;
  pmk.flags = BRCMF_WSEC_PASSPHRASE;
  for (i = 0; i < pmk_len; i++) {
    // TODO(cphoenix): Make sure handling of pmk keys is consistent with their being
    // binary values, not ASCII chars.
    snprintf((char*)&pmk.key[2 * i], 3, "%02x", pmk_data[i]);
  }

  /* store psk in firmware */
  err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_WSEC_PMK, &pmk, sizeof(pmk), nullptr);
  if (err != ZX_OK) {
    BRCMF_ERR("failed to change PSK in firmware (len=%u)", pmk_len);
  }

  return err;
}

static void brcmf_notify_deauth(struct net_device* ndev, const uint8_t peer_sta_address[ETH_ALEN]) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping deauth confirm callback");
    return;
  }

  wlan_fullmac_deauth_confirm_t resp = {};
  memcpy(resp.peer_sta_address, peer_sta_address, ETH_ALEN);

  BRCMF_IFDBG(WLANIF, ndev, "Sending deauth confirm to SME.");
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC "", FMT_MAC_ARGS(peer_sta_address));
#endif /* !defined(NDEBUG) */

  wlan_fullmac_impl_ifc_deauth_conf(&ndev->if_proto, &resp);
}

static void brcmf_notify_disassoc(struct net_device* ndev, zx_status_t status) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping disassoc confirm callback");
    return;
  }

  wlan_fullmac_disassoc_confirm_t resp = {};
  resp.status = status;
  BRCMF_IFDBG(WLANIF, ndev, "Sending disassoc confirm to SME. status: %" PRIu32 "", status);
  wlan_fullmac_impl_ifc_disassoc_conf(&ndev->if_proto, &resp);
}

// Send deauth_ind to SME (can be from client or softap)
static void brcmf_notify_deauth_ind(net_device* ndev, const uint8_t mac_addr[ETH_ALEN],
                                    wlan_ieee80211::ReasonCode reason_code,
                                    bool locally_initiated) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping deauth ind callback");
    return;
  }

  wlan_fullmac_deauth_indication_t ind = {};

  BRCMF_IFDBG(WLANIF, ndev, "Link Down: Sending deauth ind to SME. reason: %d",
              static_cast<int>(reason_code));
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC "", FMT_MAC_ARGS(mac_addr));
#endif /* !defined(NDEBUG) */

  memcpy(ind.peer_sta_address, mac_addr, ETH_ALEN);
  ind.reason_code = static_cast<reason_code_t>(reason_code);
  ind.locally_initiated = locally_initiated;
  wlan_fullmac_impl_ifc_deauth_ind(&ndev->if_proto, &ind);
}

// Send disassoc_ind to SME (can be from client or softap)
static void brcmf_notify_disassoc_ind(net_device* ndev, const uint8_t mac_addr[ETH_ALEN],
                                      wlan_ieee80211::ReasonCode reason_code,
                                      bool locally_initiated) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping disassoc ind callback");
    return;
  }

  wlan_fullmac_disassoc_indication_t ind = {};

  BRCMF_IFDBG(WLANIF, ndev, "Link Down: Sending disassoc ind to SME. reason: %d",
              static_cast<int>(reason_code));
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC ", ", FMT_MAC_ARGS(mac_addr));
#endif /* !defined(NDEBUG) */

  memcpy(ind.peer_sta_address, mac_addr, ETH_ALEN);
  ind.reason_code = static_cast<reason_code_t>(reason_code);
  ind.locally_initiated = locally_initiated;
  wlan_fullmac_impl_ifc_disassoc_ind(&ndev->if_proto, &ind);
}

static void cfg80211_disconnected(struct brcmf_cfg80211_vif* vif,
                                  wlan_ieee80211::ReasonCode reason_code, uint16_t event_code) {
  struct net_device* ndev = vif->wdev.netdev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping link down callback");
    return;
  }

  struct brcmf_cfg80211_info* cfg = vif->ifp->drvr->config;
  BRCMF_DBG(CONN, "Link Down: address: " FMT_MAC ", SME reason: %d",
            FMT_MAC_ARGS(vif->profile.bssid), static_cast<int>(reason_code));

  const bool sme_initiated_deauth =
      cfg->disconnect_mode == BRCMF_DISCONNECT_DEAUTH &&
      (event_code == BRCMF_E_DEAUTH || event_code == BRCMF_E_DISASSOC);
  const bool sme_initiated_disassoc =
      cfg->disconnect_mode == BRCMF_DISCONNECT_DISASSOC &&
      (event_code == BRCMF_E_DEAUTH || event_code == BRCMF_E_DISASSOC);

  if (sme_initiated_deauth) {
    brcmf_notify_deauth(ndev, vif->profile.bssid);
  } else if (sme_initiated_disassoc) {
    brcmf_notify_disassoc(ndev, ZX_OK);
  } else {
    const bool locally_initiated = event_code == BRCMF_E_DEAUTH || event_code == BRCMF_E_DISASSOC ||
                                   event_code == BRCMF_E_LINK;
    // BRCMF_E_DEAUTH is unlikely if not SME-initiated
    if (event_code == BRCMF_E_DEAUTH || event_code == BRCMF_E_DEAUTH_IND ||
        event_code == BRCMF_E_LINK) {
      brcmf_notify_deauth_ind(ndev, vif->profile.bssid, reason_code, locally_initiated);
    } else {
      // This is a catch-all case - could be E_DISASSOC, E_DISASSOC_IND or IF delete
      brcmf_notify_disassoc_ind(ndev, vif->profile.bssid, reason_code, locally_initiated);
    }
  }
  cfg->disconnect_mode = BRCMF_DISCONNECT_NONE;
}

// Bring the IF down. Synaptics recommends using this to reset the IF after a
// connection attempt.
static zx_status_t brcmf_bss_reset(brcmf_if* ifp) {
  bcme_status_t fw_err;

  zx_status_t status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("BRCMF_C_DOWN error %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
  }

  status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("BRCMF_C_UP error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
  }
  return status;
}

static void brcmf_link_down(struct brcmf_cfg80211_vif* vif, wlan_ieee80211::ReasonCode reason_code,
                            uint16_t event_code) {
  struct brcmf_cfg80211_info* cfg = vif->ifp->drvr->config;
  zx_status_t err = ZX_OK;

  BRCMF_DBG(TRACE, "Enter");

  if (brcmf_test_and_clear_bit(brcmf_vif_status_bit_t::CONNECTED, &vif->sme_state)) {
    BRCMF_INFO("Link down while connected.");
    bcme_status_t fwerr = BCME_OK;

    // Calling WLC_DISASSOC to stop excess roaming
    err = brcmf_fil_cmd_data_set(vif->ifp, BRCMF_C_DISASSOC, nullptr, 0, &fwerr);
    if (err != ZX_OK) {
      BRCMF_ERR("WLC_DISASSOC failed: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fwerr));
    }
    if (vif->wdev.iftype == WLAN_MAC_ROLE_CLIENT) {
      cfg80211_disconnected(vif, reason_code, event_code);
    }
  }
  brcmf_bss_reset(vif->ifp);

  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTING, &vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTED, &vif->sme_state);
  brcmf_clear_bit(brcmf_scan_status_bit_t::SUPPRESS, &cfg->scan_status);
  brcmf_btcoex_set_mode(vif, BRCMF_BTCOEX_ENABLED, 0);
  if (vif->profile.use_fwsup != BRCMF_PROFILE_FWSUP_NONE) {
    brcmf_set_pmk(vif->ifp, nullptr, 0);
    vif->profile.use_fwsup = BRCMF_PROFILE_FWSUP_NONE;
  }
  BRCMF_DBG(TRACE, "Exit");
}

static zx_status_t brcmf_set_auth_type(struct net_device* ndev, uint8_t auth_type) {
  brcmf_if* ifp = ndev_to_if(ndev);
  int32_t val = 0;
  zx_status_t status = ZX_OK;

  switch (auth_type) {
    case WLAN_AUTH_TYPE_OPEN_SYSTEM:
      val = BRCMF_AUTH_MODE_OPEN;
      break;
    case WLAN_AUTH_TYPE_SHARED_KEY:
      // When asked to use a shared key (which should only happen for WEP), we will direct the
      // firmware to use auto-detect, which will fall back on open WEP if shared WEP fails to
      // succeed. This was chosen to allow us to avoid implementing WEP auto-detection at higher
      // levels of the wlan stack.
      val = BRCMF_AUTH_MODE_AUTO;
      break;
    case WLAN_AUTH_TYPE_SAE:
      val = BRCMF_AUTH_MODE_SAE;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  BRCMF_DBG(CONN, "setting auth to %d", val);
  status = brcmf_fil_bsscfg_int_set(ifp, "auth", val);
  if (status != ZX_OK) {
    BRCMF_ERR("set auth failed (%s)", zx_status_get_string(status));
  }
  return status;
}

static bool brcmf_valid_wpa_oui(uint8_t* oui, bool is_rsn_ie) {
  if (is_rsn_ie) {
    return (memcmp(oui, RSN_OUI, TLV_OUI_LEN) == 0);
  }

  return (memcmp(oui, MSFT_OUI, TLV_OUI_LEN) == 0);
}

static zx_status_t brcmf_configure_wpaie(struct brcmf_if* ifp, const struct brcmf_vs_tlv* wpa_ie,
                                         bool is_rsn_ie, bool is_ap) {
  uint16_t count;
  zx_status_t err = ZX_OK;
  int32_t len;
  uint32_t i;
  uint32_t wsec;
  uint32_t pval = 0;
  uint32_t gval = 0;
  uint32_t wpa_auth = 0;
  uint32_t offset;
  uint8_t* data;
  uint16_t rsn_cap;
  uint32_t wme_bss_disable;
  uint32_t mfp;

  BRCMF_DBG(TRACE, "Enter");
  if (wpa_ie == nullptr) {
    goto exit;
  }
  len = wpa_ie->len + TLV_HDR_LEN;
  data = (uint8_t*)wpa_ie;
  offset = TLV_HDR_LEN;
  if (!is_rsn_ie) {
    offset += VS_IE_FIXED_HDR_LEN;
  } else {
    offset += WPA_IE_VERSION_LEN;
  }

  /* check for multicast cipher suite */
  if ((int32_t)offset + WPA_IE_MIN_OUI_LEN > len) {
    err = ZX_ERR_INVALID_ARGS;
    BRCMF_ERR("no multicast cipher suite");
    goto exit;
  }

  if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
    err = ZX_ERR_INVALID_ARGS;
    BRCMF_ERR("invalid OUI");
    goto exit;
  }
  offset += TLV_OUI_LEN;

  /* pick up multicast cipher */
  switch (data[offset]) {
    case WPA_CIPHER_NONE:
      BRCMF_DBG(CONN, "MCAST WPA CIPHER NONE");
      gval = WSEC_NONE;
      break;
    case WPA_CIPHER_WEP_40:
    case WPA_CIPHER_WEP_104:
      BRCMF_DBG(CONN, "MCAST WPA CIPHER WEP40/104");
      gval = WEP_ENABLED;
      break;
    case WPA_CIPHER_TKIP:
      BRCMF_DBG(CONN, "MCAST WPA CIPHER TKIP");
      gval = TKIP_ENABLED;
      break;
    case WPA_CIPHER_CCMP_128:
      BRCMF_DBG(CONN, "MCAST WPA CIPHER CCMP 128");
      gval = AES_ENABLED;
      break;
    default:
      err = ZX_ERR_INVALID_ARGS;
      BRCMF_ERR("Invalid multi cast cipher info");
      goto exit;
  }

  offset++;
  /* walk thru unicast cipher list and pick up what we recognize */
  count = data[offset] + (data[offset + 1] << 8);
  offset += WPA_IE_SUITE_COUNT_LEN;
  /* Check for unicast suite(s) */
  if ((int32_t)(offset + (WPA_IE_MIN_OUI_LEN * count)) > len) {
    err = ZX_ERR_INVALID_ARGS;
    BRCMF_ERR("no unicast cipher suite");
    goto exit;
  }
  for (i = 0; i < count; i++) {
    if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
      err = ZX_ERR_INVALID_ARGS;
      BRCMF_ERR("ivalid OUI");
      goto exit;
    }
    offset += TLV_OUI_LEN;
    switch (data[offset]) {
      case WPA_CIPHER_NONE:
        BRCMF_DBG(CONN, "UCAST WPA CIPHER NONE");
        break;
      case WPA_CIPHER_WEP_40:
      case WPA_CIPHER_WEP_104:
        BRCMF_DBG(CONN, "UCAST WPA CIPHER WEP 40/104");
        pval |= WEP_ENABLED;
        break;
      case WPA_CIPHER_TKIP:
        BRCMF_DBG(CONN, "UCAST WPA CIPHER TKIP");
        pval |= TKIP_ENABLED;
        break;
      case WPA_CIPHER_CCMP_128:
        BRCMF_DBG(CONN, "UCAST WPA CIPHER CCMP 128");
        pval |= AES_ENABLED;
        break;
      default:
        BRCMF_DBG(CONN, "Invalid unicast security info");
    }
    offset++;
  }
  /* walk thru auth management suite list and pick up what we recognize */
  count = data[offset] + (data[offset + 1] << 8);
  offset += WPA_IE_SUITE_COUNT_LEN;
  /* Check for auth key management suite(s) */
  if ((int32_t)(offset + (WPA_IE_MIN_OUI_LEN * count)) > len) {
    err = ZX_ERR_INVALID_ARGS;
    BRCMF_ERR("no auth key mgmt suite");
    goto exit;
  }
  for (i = 0; i < count; i++) {
    if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
      err = ZX_ERR_INVALID_ARGS;
      BRCMF_ERR("ivalid OUI");
      goto exit;
    }
    offset += TLV_OUI_LEN;
    switch (data[offset]) {
      case RSN_AKM_NONE:
        BRCMF_DBG(CONN, "RSN_AKM_NONE");
        wpa_auth |= WPA_AUTH_NONE;
        break;
      case RSN_AKM_UNSPECIFIED:
        BRCMF_DBG(CONN, "RSN_AKM_UNSPECIFIED");
        is_rsn_ie ? (wpa_auth |= WPA2_AUTH_UNSPECIFIED) : (wpa_auth |= WPA_AUTH_UNSPECIFIED);
        break;
      case RSN_AKM_PSK:
        BRCMF_DBG(CONN, "RSN_AKM_PSK");
        is_rsn_ie ? (wpa_auth |= WPA2_AUTH_PSK) : (wpa_auth |= WPA_AUTH_PSK);
        break;
      case RSN_AKM_SHA256_PSK:
        BRCMF_DBG(CONN, "RSN_AKM_MFP_PSK");
        wpa_auth |= WPA2_AUTH_PSK_SHA256;
        break;
      case RSN_AKM_SHA256_1X:
        BRCMF_DBG(CONN, "RSN_AKM_MFP_1X");
        wpa_auth |= WPA2_AUTH_1X_SHA256;
        break;
      case RSN_AKM_SAE_PSK:
        BRCMF_DBG(CONN, "RSN_AKM_SAE");
        wpa_auth |= WPA3_AUTH_SAE_PSK;
        break;
      default:
        BRCMF_DBG(CONN, "Invalid key mgmt info, the auth mgmt suite is %u", data[offset]);
    }
    offset++;
  }

  /* Don't set SES_OW_ENABLED for now (since we don't support WPS yet) */
  wsec = (pval | gval);
  BRCMF_INFO("WSEC: 0x%x WPA AUTH: 0x%x", wsec, wpa_auth);

  /* set wsec */
  err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
  if (err != ZX_OK) {
    BRCMF_ERR("wsec error %d", err);
    goto exit;
  }

  mfp = BRCMF_MFP_NONE;
  if (is_rsn_ie) {
    if (is_ap) {
      wme_bss_disable = 1;
      if (((int32_t)offset + RSN_CAP_LEN) <= len) {
        rsn_cap = data[offset] + (data[offset + 1] << 8);
        if (rsn_cap & RSN_CAP_PTK_REPLAY_CNTR_MASK) {
          wme_bss_disable = 0;
        }
        if (rsn_cap & RSN_CAP_MFPR_MASK) {
          BRCMF_DBG(TRACE, "MFP Required");
          mfp = BRCMF_MFP_REQUIRED;
          /* Firmware only supports mfp required in
           * combination with WPA2_AUTH_PSK_SHA256 or
           * WPA2_AUTH_1X_SHA256.
           */
          if (!(wpa_auth & (WPA2_AUTH_PSK_SHA256 | WPA2_AUTH_1X_SHA256))) {
            err = ZX_ERR_INVALID_ARGS;
            goto exit;
          }
          /* Firmware has requirement that WPA2_AUTH_PSK/
           * WPA2_AUTH_UNSPECIFIED be set, if SHA256 OUI
           * is to be included in the rsn ie.
           */
          if (wpa_auth & WPA2_AUTH_PSK_SHA256) {
            wpa_auth |= WPA2_AUTH_PSK;
          } else if (wpa_auth & WPA2_AUTH_1X_SHA256) {
            wpa_auth |= WPA2_AUTH_UNSPECIFIED;
          }
        } else if (rsn_cap & RSN_CAP_MFPC_MASK) {
          BRCMF_DBG(TRACE, "MFP Capable");
          mfp = BRCMF_MFP_CAPABLE;
        }
      }
      offset += RSN_CAP_LEN;
      /* set wme_bss_disable to sync RSN Capabilities */
      err = brcmf_fil_bsscfg_int_set(ifp, "wme_bss_disable", wme_bss_disable);
      if (err != ZX_OK) {
        BRCMF_ERR("wme_bss_disable error %d", err);
        goto exit;
      }

      /* Skip PMKID cnt as it is know to be 0 for AP. */
      offset += RSN_PMKID_COUNT_LEN;

      /* See if there is BIP wpa suite left for MFP */
      if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFP) &&
          ((int32_t)(offset + WPA_IE_MIN_OUI_LEN) <= len)) {
        err = brcmf_fil_bsscfg_data_set(ifp, "bip", &data[offset], WPA_IE_MIN_OUI_LEN);
        if (err != ZX_OK) {
          BRCMF_ERR("bip error %d", err);
          goto exit;
        }
      }
    } else if (wpa_auth & (WPA3_AUTH_SAE_PSK | WPA2_AUTH_PSK)) {
      // Set mfp to capable if it's a wpa2 or wpa3 assocation.
      mfp = BRCMF_MFP_CAPABLE;
    }
  }

  /* Configure MFP, just a reminder, this needs to go after wsec otherwise the wsec command
   * will overwrite the values set by MFP
   */
  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFP)) {
    err = brcmf_fil_bsscfg_int_set(ifp, "mfp", mfp);
    if (err != ZX_OK) {
      BRCMF_ERR("mfp error %s", zx_status_get_string(err));
      goto exit;
    }
  }

  /* set upper-layer auth */
  err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_auth);
  if (err != ZX_OK) {
    BRCMF_ERR("wpa_auth error %d", err);
    goto exit;
  }

exit:
  return err;
}

static zx_status_t brcmf_configure_opensecurity(struct brcmf_if* ifp) {
  zx_status_t err;
  int32_t wpa_val;

  /* set wsec */
  BRCMF_DBG(CONN, "Setting wsec to 0");
  err = brcmf_fil_bsscfg_int_set(ifp, "wsec", 0);
  if (err != ZX_OK) {
    BRCMF_ERR("wsec error %d", err);
    return err;
  }
  /* set upper-layer auth */
  wpa_val = WPA_AUTH_DISABLED;
  BRCMF_DBG(CONN, "Setting wpa_auth to %d", wpa_val);
  err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_val);
  if (err != ZX_OK) {
    BRCMF_ERR("wpa_auth error %d", err);
    return err;
  }

  return ZX_OK;
}

// Retrieve information about the station with the specified MAC address. Note that
// association ID is only available when operating in AP mode (for our clients).
static zx_status_t brcmf_cfg80211_get_station(struct net_device* ndev, const uint8_t* mac,
                                              struct brcmf_sta_info_le* sta_info_le) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  zx_status_t err = ZX_OK;

  BRCMF_DBG(TRACE, "Enter, MAC " FMT_MAC, FMT_MAC_ARGS(mac));
  if (!check_vif_up(ifp->vif)) {
    return ZX_ERR_IO;
  }

  memset(sta_info_le, 0, sizeof(*sta_info_le));
  memcpy(sta_info_le, mac, ETH_ALEN);

  // First, see if we have a TDLS peer
  err = brcmf_fil_iovar_data_get(ifp, "tdls_sta_info", sta_info_le, sizeof(*sta_info_le), nullptr);
  if (err != ZX_OK) {
    bcme_status_t fw_err = BCME_OK;
    err = brcmf_fil_iovar_data_get(ifp, "sta_info", sta_info_le, sizeof(*sta_info_le), &fw_err);
    if (err != ZX_OK) {
      BRCMF_ERR("GET STA INFO failed: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
    }
  }
  BRCMF_DBG(TRACE, "Exit");
  return err;
}

static inline bool brcmf_tlv_ie_has_msft_type(const uint8_t* ie, uint8_t oui_type) {
  return (ie[TLV_LEN_OFF] >= TLV_OUI_LEN + TLV_OUI_TYPE_LEN &&
          !memcmp(&ie[TLV_BODY_OFF], MSFT_OUI, TLV_OUI_LEN) &&
          // The byte after OUI is OUI type
          ie[TLV_BODY_OFF + TLV_OUI_LEN] == oui_type);
}

void brcmf_return_assoc_result(struct net_device* ndev, status_code_t status_code) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping association callback");
    return;
  }

  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  wlan_fullmac_connect_confirm_t conf;
  conf.result_code = status_code;
  if (conf.result_code == STATUS_CODE_SUCCESS && cfg->conn_info.resp_ie_len > 0) {
    BRCMF_DBG(TEMP, " * Hard-coding association_id to 42; this will likely break something!");
    uint16_t association_id = 42;  // TODO: Use brcmf_cfg80211_get_station() to get aid
    conf.association_id = association_id;
    conf.association_ies_count = cfg->conn_info.resp_ie_len;
    conf.association_ies_list = cfg->conn_info.resp_ie;
  } else {
    conf.association_id = 0;
    conf.association_ies_count = 0;
  }

  BRCMF_IFDBG(WLANIF, ndev, "Sending connect result to SME. result: %" PRIu16 ", aid: %" PRIu16,
              conf.result_code, conf.association_id);
  wlan_fullmac_impl_ifc_connect_conf(&ndev->if_proto, &conf);
}

std::vector<uint8_t> brcmf_find_ssid_in_ies(const uint8_t* ie, size_t ie_len) {
  std::vector<uint8_t> ssid;
  size_t offset = 0;
  while (offset < ie_len) {
    uint8_t type = ie[offset];
    uint8_t length = ie[offset + TLV_LEN_OFF];
    if (type == WLAN_IE_TYPE_SSID) {
      size_t ssid_len = std::min<size_t>(length, ie_len - (offset + TLV_HDR_LEN));
      ssid_len = std::min<size_t>(ssid_len, wlan_ieee80211::MAX_SSID_BYTE_LEN);
      auto start = ie + offset + TLV_HDR_LEN;
      ssid = std::vector<uint8_t>(start, start + ssid_len);
      break;
    }
    offset += length + TLV_HDR_LEN;
  }
  return ssid;
}

zx_status_t brcmf_cfg80211_connect(struct net_device* ndev, const wlan_fullmac_connect_req_t* req) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct brcmf_join_params join_params;
  wlan_channel_t chan_override;
  uint16_t chanspec;
  size_t join_params_size = 0;
  std::vector<uint8_t> ssid;
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;
  bool is_rsn_ie = true;

  BRCMF_DBG(TRACE, "Enter");
  if (!check_vif_up(ifp->vif)) {
    return ZX_ERR_IO;
  }

  // Wait until disconnect completes before proceeding with the connect.
  if (sync_completion_wait(&ifp->disconnect_done, BRCMF_WAIT_FOR_DISCONNECT_MSEC) != ZX_OK) {
    BRCMF_ERR("Timed out waiting for client disconnect");
    goto fail;
  }
  // Firmware is already processing a connect request. Don't clear the CONNECTING bit because the
  // operation is still expected to complete.
  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state)) {
    err = ZX_ERR_BAD_STATE;
    BRCMF_WARN("Connection not possible. Another connection attempt in progress.");
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
    goto done;
  }

  if (req->security_ie_count >= TLV_HDR_LEN &&
      req->security_ie_list[0] == WLAN_IE_TYPE_VENDOR_SPECIFIC &&
      brcmf_tlv_ie_has_msft_type(req->security_ie_list, WPA_OUI_TYPE)) {
    // Not RSNE but is WPA1 IE
    is_rsn_ie = false;
  }

  if (req->security_ie_count > 0) {
    // Set wpaie only if there's security ie
    err = brcmf_fil_iovar_data_set(ifp, "wpaie", req->security_ie_list, req->security_ie_count,
                                   &fw_err);
    if (err != ZX_OK) {
      BRCMF_ERR("wpaie failed: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
      goto fail;
    }
  }

  // TODO(fxbug.dev/29354): We should be getting the IEs from SME. Passing a null entry seems
  // to work for now, presumably because the firmware uses its defaults.
  err = brcmf_vif_set_mgmt_ie(ifp->vif, BRCMF_VNDR_IE_ASSOCREQ_FLAG, nullptr, 0);
  if (err != ZX_OK) {
    BRCMF_ERR("Set Assoc REQ IE Failed");
  } else {
    BRCMF_DBG(TRACE, "Applied Vndr IEs for Assoc request");
  }

  if (req->security_ie_count > 0) {
    struct brcmf_vs_tlv* tmp_ie = (struct brcmf_vs_tlv*)req->security_ie_list;
    err = brcmf_configure_wpaie(ifp, tmp_ie, is_rsn_ie, false);
    if (err != ZX_OK) {
      BRCMF_ERR("Failed to install RSNE: %s", zx_status_get_string(err));
      goto fail;
    }
  }

  brcmf_set_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state);

  // Override the channel bandwidth with 20Mhz because `channel_to_chanspec` doesn't support
  // encoding 80Mhz and the upper layer had always passed 20Mhz historically so also need to
  // test whether the 40Mhz encoding works properly.
  // TODO(fxbug.dev/65770) - Remove this override.
  chan_override = ifp->connect_req.selected_bss.channel;
  chan_override.cbw = CHANNEL_BANDWIDTH_CBW20;

  chanspec = channel_to_chanspec(&cfg->d11inf, &chan_override);
  cfg->channel = chanspec;

  ssid = brcmf_find_ssid_in_ies(ifp->connect_req.selected_bss.ies_list,
                                ifp->connect_req.selected_bss.ies_count);

  join_params_size = sizeof(join_params);
  memset(&join_params, 0, join_params_size);

  memcpy(&join_params.ssid_le.SSID, ssid.data(), ssid.size());
  join_params.ssid_le.SSID_len = ssid.size();

  memcpy(join_params.params_le.bssid, ifp->connect_req.selected_bss.bssid, ETH_ALEN);
  join_params.params_le.chanspec_num = 1;
  join_params.params_le.chanspec_list[0] = chanspec;

  // Attempt to clear counters here and ignore the error. Synaptics indicates that
  // some counters might be active even when the client is not connected.
  brcmf_fil_iovar_data_get(ifp, "reset_cnts", nullptr, 0, &fw_err);
  brcmf_fil_iovar_data_set(ifp, "wme_clear_counters", nullptr, 0, &fw_err);
  BRCMF_DBG(CONN, "Sending C_SET_SSID to FW");
  err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID, &join_params, join_params_size, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("join failed (%d)", err);
  } else {
    BRCMF_IFDBG(WLANIF, ndev, "Connect timer started.");
    cfg->connect_timer->Start(BRCMF_CONNECT_TIMER_DUR_MS);
  }

fail:
  if (err != ZX_OK) {
    brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state);
    BRCMF_DBG(CONN, "Failed during join: %s", zx_status_get_string(err));
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
  }

done:
  BRCMF_DBG(TRACE, "Exit");
  return err;
}

static zx_status_t brcmf_get_ctrl_channel(brcmf_if* ifp, uint16_t* chanspec_out,
                                          uint8_t* ctl_chan_out) {
  bcme_status_t fw_err;
  zx_status_t err;

  // Get chanspec of the given IF from firmware.
  err = brcmf_fil_iovar_data_get(ifp, "chanspec", chanspec_out, sizeof(uint16_t), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to retrieve chanspec: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    return err;
  }

  // Get the control channel given chanspec
  err = chspec_ctlchan(*chanspec_out, ctl_chan_out);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to get control channel from chanspec: 0x%x status: %s", *chanspec_out,
              zx_status_get_string(err));
    return err;
  }
  return ZX_OK;
}

// Log driver and FW packet counters along with current channel and signal strength
static void brcmf_log_client_stats(struct brcmf_cfg80211_info* cfg) {
  struct net_device* ndev = cfg_to_ndev(cfg);
  struct brcmf_if* ifp = ndev_to_if(ndev);
  bcme_status_t fw_err;
  uint32_t is_up = 0;
  float tx_err_rate = 0.0, rx_err_rate = 0.0;

  // First check if the IF is up.
  zx_status_t err =
      brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_IS_IF_UP, &is_up, sizeof(is_up), &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("Unable to get IF status: %s fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
  }
  // Get channel information from firmware.
  uint16_t chanspec;
  uint8_t ctl_chan = 0;
  err = brcmf_get_ctrl_channel(ifp, &chanspec, &ctl_chan);

  // Get the current rate
  uint32_t fw_rate = 0;
  float real_rate = 0.0;
  err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_RATE, &fw_rate, sizeof(fw_rate), &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("Unable to get rate: %s fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
  } else {
    real_rate = BRCMF_CONVERT_TO_REAL_RATE(fw_rate);
  }

  // Get the current noise floor
  int32_t noise = 0;
  err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_PHY_NOISE, &noise, sizeof(noise), &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("Unable to get noise: %s fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
  }
  zxlogf(INFO,
         "Client IF up: %d channel: %d Rate: %.2f Mbps RSSI: %d dBm SNR: %d dB  noise: %d dBm",
         is_up, ctl_chan, real_rate, ndev->last_known_rssi_dbm, ndev->last_known_snr_db, noise);

  // Get the FW packet counts
  brcmf_pktcnt_le fw_pktcnt = {};
  err =
      brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_GET_PKTCNTS, &fw_pktcnt, sizeof(fw_pktcnt), &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("Unable to get FW packet counts err: %s fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
  } else {
    zxlogf(INFO, "FW Stats: Rx - Good: %d Bad: %d Ocast: %d; Tx - Good: %d Bad: %d",
           fw_pktcnt.rx_good_pkt, fw_pktcnt.rx_bad_pkt, fw_pktcnt.rx_ocast_good_pkt,
           fw_pktcnt.tx_good_pkt, fw_pktcnt.tx_bad_pkt);

    float total_rx_pkts = fw_pktcnt.rx_good_pkt + fw_pktcnt.rx_bad_pkt;
    float total_tx_pkts = fw_pktcnt.tx_good_pkt + fw_pktcnt.tx_bad_pkt;
    rx_err_rate = tx_err_rate = 0;
    if (total_rx_pkts > 0) {
      rx_err_rate = (float)(fw_pktcnt.rx_bad_pkt) / total_rx_pkts;
    }
    if (total_tx_pkts > 0) {
      tx_err_rate = (float)(fw_pktcnt.tx_bad_pkt) / total_tx_pkts;
    }
  }

  if (ndev->stats.rx_packets != ndev->stats.rx_last_log) {
    if (ndev->stats.rx_packets < ndev->stats.rx_last_log) {
      BRCMF_INFO(
          "Current value for rx_packets is smaller than the last one, an overflow might happened.");
    }
    // Clear the freeze count once the device gets out of the bad state.
    ndev->stats.rx_freeze_count = 0;
  } else if (ndev->stats.tx_packets > ndev->stats.tx_last_log) {
    // Increase the rx freeze count only when tx_packets is still increasing while rx_packets
    // is unchanged. This pattern is expected if a scan happens when the device is not connected to
    // an AP, but this function will not be called in this case, so no false positive will occur.
    ndev->stats.rx_freeze_count++;
  }

  // Update driver rx and tx count cached from last log.
  ndev->stats.rx_last_log = ndev->stats.rx_packets;
  ndev->stats.tx_last_log = ndev->stats.tx_packets;

  // Increase inspect counter when the rx freeze counter first reaches threshold.
  if (ndev->stats.rx_freeze_count == BRCMF_RX_FREEZE_THRESHOLD / BRCMF_CONNECT_LOG_DUR) {
    // Note the rx freeze in the inspect logs
    ifp->drvr->device->GetInspect()->LogRxFreeze();
  }

  // The reason for using larger or equal here is to make sure the deauth can be triggered again
  // after the limitation time passes.
  if (ndev->stats.rx_freeze_count >= BRCMF_RX_FREEZE_THRESHOLD / BRCMF_CONNECT_LOG_DUR) {
    // Trigger a deauth, unless we have exceeded our maximum rate
    // (BRCMF_RX_FREEZE_MAX_DEAUTHS_PER_HOUR) within time limitation.
    bool trigger_deauth = false;
    uint32_t current_log_count = ndev->client_stats_log_count;
    std::list<uint32_t>* deauth_times = &ndev->rx_freeze_deauth_times;

    if (deauth_times->size() < BRCMF_RX_FREEZE_MAX_DEAUTHS_PER_HOUR) {
      // Our total number of deauth's is less than the per-hour limit
      trigger_deauth = true;
    } else {
      uint32_t oldest_deauth_time = deauth_times->front();
      if ((current_log_count - oldest_deauth_time) > ZX_HOUR(1) / BRCMF_CONNECT_LOG_DUR) {
        // It has been more than an hour since our oldest recorded deauth
        trigger_deauth = true;
        deauth_times->pop_front();
      }
    }

    if (trigger_deauth) {
      // Disassociate
      BRCMF_ERR("No rx frames received in %zu seconds, triggering deauthentication",
                static_cast<size_t>(BRCMF_RX_FREEZE_THRESHOLD) / ZX_SEC(1));
      // Reset the rx freeze count when deauth is triggered, waiting for the next trigger.
      ndev->stats.rx_freeze_count = 0;
      brcmf_link_down(ifp->vif, wlan_ieee80211::ReasonCode::FW_RX_STALLED, BRCMF_E_DEAUTH_IND);
      deauth_times->push_back(current_log_count);
    }
  }

  zxlogf(INFO, "Driver Stats: Rx - Good: %d Bad: %d; Tx - Sent to FW: %d Conf: %d Drop: %d Bad: %d",
         ndev->stats.rx_packets, ndev->stats.rx_errors, ndev->stats.tx_packets,
         ndev->stats.tx_confirmed, ndev->stats.tx_dropped, ndev->stats.tx_errors);

  // Get the WME counters
  wl_wme_cnt_t wme_cnt;
  err = brcmf_fil_iovar_data_get(ifp, "wme_counters", &wme_cnt, sizeof(wl_wme_cnt_t), &fw_err);
  if (err != ZX_OK) {
    BRCMF_INFO("Unable to get WME counters err: %s fw err %s", zx_status_get_string(err),
               brcmf_fil_get_errstr(fw_err));
  } else {
    zxlogf(INFO, "WME counters - Rx: %d; Rx Bad: %d; Tx: %d; Tx Bad: %d",
           wme_cnt.rx[AC_VO].packets + wme_cnt.rx[AC_VI].packets + wme_cnt.rx[AC_BE].packets +
               wme_cnt.rx[AC_BK].packets,
           wme_cnt.rx_failed[AC_VO].packets + wme_cnt.rx_failed[AC_VI].packets +
               wme_cnt.rx_failed[AC_BE].packets + wme_cnt.rx_failed[AC_BK].packets,
           wme_cnt.tx[AC_VO].packets + wme_cnt.tx[AC_VI].packets + wme_cnt.tx[AC_BE].packets +
               wme_cnt.tx[AC_BK].packets,
           wme_cnt.tx_failed[AC_VO].packets + wme_cnt.tx_failed[AC_VI].packets +
               wme_cnt.tx_failed[AC_BE].packets + wme_cnt.tx_failed[AC_BK].packets);
    zxlogf(INFO, "VO AC - Rx: %d; Rx Bad: %d; Tx: %d; Tx Bad: %d", wme_cnt.rx[AC_VO].packets,
           wme_cnt.rx_failed[AC_VO].packets, wme_cnt.tx[AC_VO].packets,
           wme_cnt.tx_failed[AC_VO].packets);
    zxlogf(INFO, "VI AC - Rx: %d; Rx Bad: %d; Tx: %d; Tx Bad: %d", wme_cnt.rx[AC_VI].packets,
           wme_cnt.rx_failed[AC_VI].packets, wme_cnt.tx[AC_VI].packets,
           wme_cnt.tx_failed[AC_VI].packets);
    zxlogf(INFO, "BE AC - Rx: %d; Rx Bad: %d; Tx: %d; Tx Bad: %d", wme_cnt.rx[AC_BE].packets,
           wme_cnt.rx_failed[AC_BE].packets, wme_cnt.tx[AC_BE].packets,
           wme_cnt.tx_failed[AC_BE].packets);
    zxlogf(INFO, "BK AC - Rx: %d; Rx Bad: %d; Tx: %d; Tx Bad: %d", wme_cnt.rx[AC_BK].packets,
           wme_cnt.rx_failed[AC_BK].packets, wme_cnt.tx[AC_BK].packets,
           wme_cnt.tx_failed[AC_BK].packets);
  }

  brcmf_bus_log_stats(cfg->pub->bus_if);
  // If the client is connected to a 2.4 GHz channel, log some BT Coex related info
  if (ctl_chan <= CH_MAX_2G_CHANNEL) {
    brcmf_btcoex_log_active_bt_tasks(ifp);
  }

  // If the rate is 6 Mbps or less OR Rx error rate >= 15% OR Tx error rate is >= 15%
  // log some of the Tx and Rx error counts retrieved from FW.
  if ((real_rate != 0.0 && (real_rate <= BRCMF_LOW_DATA_RATE_THRESHOLD)) ||
      rx_err_rate >= BRCMF_HIGH_ERR_RATE_THRESHOLD ||
      tx_err_rate >= BRCMF_HIGH_ERR_RATE_THRESHOLD) {
    uint8_t cnt_buf[BRCMF_DCMD_MAXLEN] = {0};
    // If data rate is at or below threshold, increment the counter.
    if (real_rate != 0.0 && (real_rate <= BRCMF_LOW_DATA_RATE_THRESHOLD)) {
      ndev->stats.low_data_rate_count++;
    } else if (real_rate != 0.0) {
      ndev->stats.low_data_rate_count = 0;
    }
    // Increase inspect counter when the low data rate counter first reaches threshold.
    if (ndev->stats.low_data_rate_count ==
        BRCMF_LOW_DATA_RATE_DUR_THRESHOLD / BRCMF_CONNECT_LOG_DUR) {
      // Note the low data rate in the inspect logs
      ifp->drvr->device->GetInspect()->LogLowDataRate();
    }
    if (ndev->stats.low_data_rate_count >=
        BRCMF_LOW_DATA_RATE_DUR_RESET_THRESHOLD / BRCMF_CONNECT_LOG_DUR) {
      // Reset the low data rate counter if it has been stuck for this long (so it can be logged
      // into inspect again).
      ndev->stats.low_data_rate_count = 0;
    }
    // The version # in the counters struct returned by FW is set to 10 currently but its
    // corresponding struct definition is not available. It appears each new version is a superset
    // of the previous one. So tell FW the size of the struct is that of wl_cnt_ver_11_t which is >=
    // "wl_cnt_ver_10_t".
    err = brcmf_fil_iovar_data_get(ifp, "counters", cnt_buf, sizeof(wl_cnt_ver_11_t), &fw_err);
    if (err != ZX_OK) {
      BRCMF_WARN("Unable to get fw counters err: %s fw_err %d", zx_status_get_string(err), fw_err);
    } else {
      wl_cnt_ver_6_t* counters = reinterpret_cast<wl_cnt_ver_6_t*>(cnt_buf);

      zxlogf(
          INFO,
          "FW Err Counts: Tx: Err Rate: %.2f retrans: %u err %u serr %u nobuf %u runt %u uflo %u "
          "phyerr %u fail %u noassoc %u noack %u",
          tx_err_rate * 100.0, counters->txretrans, counters->txerror, counters->txserr,
          counters->txnobuf, counters->txrunt, counters->txuflo, counters->txphyerr,
          counters->txfail, counters->txnoassoc, counters->txnoack);
      zxlogf(
          INFO,
          "FW Err Counts: Rx: Err Rate: %.2f err %u oflo %u nobuf %u runt %u fragerr %u badplcp %u "
          "crsglitch %u badfcs %u fragerr %u giant %u noscb %u badsrcmac %u",
          rx_err_rate * 100.0, counters->rxerror, counters->rxoflo, counters->rxnobuf,
          counters->rxrunt, counters->rxfragerr, counters->rxbadplcp, counters->rxcrsglitch,
          counters->rxbadfcs, counters->rxfragerr, counters->rxgiant, counters->rxnoscb,
          counters->rxbadsrcmac);
    }

    ndev->client_stats_log_count++;
  }
}

static void brcmf_disconnect_done(struct brcmf_cfg80211_info* cfg) {
  struct net_device* ndev = cfg_to_ndev(cfg);
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_profile* profile = &ifp->vif->profile;

  BRCMF_DBG(TRACE, "Enter");

  if (brcmf_test_and_clear_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state)) {
    cfg->disconnect_timer->Stop();
    if (cfg->disconnect_mode == BRCMF_DISCONNECT_DEAUTH) {
      brcmf_notify_deauth(ndev, profile->bssid);
    } else {
      brcmf_notify_disassoc(ndev, ZX_OK);
    }
    cfg->disconnect_mode = BRCMF_DISCONNECT_NONE;
  }
  if (!brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    cfg->signal_report_timer->Stop();
    // Log the client stats one last time before clearing out the counters
    brcmf_log_client_stats(cfg);
    ndev->stats = {};
    bcme_status_t fw_err;
    zx_status_t status = brcmf_fil_iovar_data_get(ifp, "reset_cnts", nullptr, 0, &fw_err);
    if (status != ZX_OK) {
      BRCMF_WARN("Failed to clear counters: %s, fw err %s", zx_status_get_string(status),
                 brcmf_fil_get_errstr(fw_err));
    }
    status = brcmf_fil_iovar_data_set(ifp, "wme_clear_counters", nullptr, 0, &fw_err);
    if (status != ZX_OK) {
      BRCMF_WARN("Failed to clear WME counters: %s, fw err %s", zx_status_get_string(status),
                 brcmf_fil_get_errstr(fw_err));
    }
  }

  BRCMF_DBG(TRACE, "Exit");
}

static zx_status_t brcmf_get_rssi_snr(net_device* ndev, int8_t* rssi_dbm, int8_t* snr_db) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  bcme_status_t fw_err = BCME_OK;
  int32_t rssi, snr;

  *rssi_dbm = *snr_db = 0;
  zx_status_t status = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_RSSI, &rssi, sizeof(rssi), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("could not get rssi: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    return status;
  }
  status = brcmf_fil_iovar_data_get(ifp, "snr", &snr, sizeof(snr), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("could not get snr: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    return status;
  }
  *rssi_dbm = rssi;
  *snr_db = snr;
  return status;
}

static void cfg80211_signal_ind(net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  brcmf_cfg80211_info* cfg = ifp->drvr->config;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping signal report indication callback");
    // Stop the timer
    cfg->signal_report_timer->Stop();
    return;
  }

  // Send signal report indication only if client is in connected state
  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
    wlan_fullmac_signal_report_indication signal_ind;
    int8_t rssi, snr;
    if (brcmf_get_rssi_snr(ndev, &rssi, &snr) == ZX_OK) {
      signal_ind.rssi_dbm = rssi;
      signal_ind.snr_db = snr;
      // Store the value in ndev (dumped out when link goes down)
      ndev->last_known_rssi_dbm = rssi;
      ndev->last_known_snr_db = snr;
      wlan_fullmac_impl_ifc_signal_report(&ndev->if_proto, &signal_ind);
    }
    cfg->connect_log_cnt++;
    if (cfg->connect_log_cnt >= BRCMF_CONNECT_LOG_COUNT) {
      // Log the stats
      brcmf_log_client_stats(cfg);
      cfg->connect_log_cnt = 0;
    }
  } else if (!brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    // If client is not connected, stop the timer
    cfg->signal_report_timer->Stop();
  }
}

static void brcmf_connect_timeout(struct brcmf_cfg80211_info* cfg) {
  cfg->pub->irq_callback_lock.lock();
  BRCMF_DBG(TRACE, "Enter");
  EXEC_TIMEOUT_WORKER(connect_timeout_work);
  cfg->pub->irq_callback_lock.unlock();
}

static void brcmf_signal_report_worker(WorkItem* work) {
  struct brcmf_cfg80211_info* cfg =
      containerof(work, struct brcmf_cfg80211_info, signal_report_work);
  struct net_device* ndev = cfg_to_ndev(cfg);
  cfg80211_signal_ind(ndev);
}

static void brcmf_signal_report_timeout(struct brcmf_cfg80211_info* cfg) {
  cfg->pub->irq_callback_lock.lock();
  BRCMF_DBG(TRACE, "Enter");
  // If it's for SIM tests, won't enqueue.
  EXEC_TIMEOUT_WORKER(signal_report_work);
  cfg->pub->irq_callback_lock.unlock();
}

static void brcmf_disconnect_timeout_worker(WorkItem* work) {
  struct brcmf_cfg80211_info* cfg =
      containerof(work, struct brcmf_cfg80211_info, disconnect_timeout_work);
  brcmf_disconnect_done(cfg);
}

static void brcmf_disconnect_timeout(struct brcmf_cfg80211_info* cfg) {
  cfg->pub->irq_callback_lock.lock();
  BRCMF_DBG(TRACE, "Enter");

  // If it's for SIM tests, won't enqueue.
  EXEC_TIMEOUT_WORKER(disconnect_timeout_work);

  cfg->pub->irq_callback_lock.unlock();
}

static zx_status_t brcmf_cfg80211_disconnect(struct net_device* ndev,
                                             const uint8_t peer_sta_address[ETH_ALEN],
                                             uint16_t reason_code, bool deauthenticate) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_profile* profile = &ifp->vif->profile;
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct brcmf_scb_val_le scbval;
  zx_status_t status = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_DBG(TRACE, "Enter. Reason code = %d", reason_code);
  if (!check_vif_up(ifp->vif)) {
    status = ZX_ERR_IO;
    goto done;
  }

  if (!brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state) &&
      !brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state)) {
    status = ZX_ERR_BAD_STATE;
    goto done;
  }

  if (memcmp(peer_sta_address, profile->bssid, ETH_ALEN)) {
    BRCMF_ERR("peer_sta_address is not matching bssid in brcmf_cfg80211_profile. ");
#if !defined(NDEBUG)
    BRCMF_DBG(CONN, "  peer_sta_address:" FMT_MAC ", bssid in profile:" FMT_MAC "",
              FMT_MAC_ARGS(peer_sta_address), FMT_MAC_ARGS(profile->bssid));
#endif /* !defined(NDEBUG) */
    status = ZX_ERR_INVALID_ARGS;
    goto done;
  }
  // In case the connection is still in progress, stop the timer
  cfg->connect_timer->Stop();

  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state);

  BRCMF_DBG(CONN, "Disconnecting");

  // Set the timer before notifying firmware as this thread might get preempted to
  // handle the response event back from firmware. Timer can be stopped if the command
  // fails.
  cfg->disconnect_timer->Start(BRCMF_DISCONNECT_TIMER_DUR_MS);

  memcpy(&scbval.ea, peer_sta_address, ETH_ALEN);
  scbval.val = reason_code;
  cfg->disconnect_mode = deauthenticate ? BRCMF_DISCONNECT_DEAUTH : BRCMF_DISCONNECT_DISASSOC;
  brcmf_set_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state);
  status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_DISASSOC, &scbval, sizeof(scbval), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to disassociate: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    brcmf_clear_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state);
    cfg->disconnect_timer->Stop();
  }

done:
  BRCMF_DBG(TRACE, "Exit");
  return status;
}

static zx_status_t brcmf_cfg80211_del_key(struct net_device* ndev, uint8_t key_idx) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_wsec_key* key;
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter");
  BRCMF_DBG(CONN, "key index (%d)", key_idx);

  if (!check_vif_up(ifp->vif)) {
    return ZX_ERR_IO;
  }

  if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
    /* we ignore this key index in this case */
    return ZX_ERR_INVALID_ARGS;
  }

  key = &ifp->vif->profile.key[key_idx];

  if (key->algo == CRYPTO_ALGO_OFF) {
    BRCMF_DBG(CONN, "Ignore clearing of (never configured) key");
    return ZX_ERR_BAD_STATE;
  }

  memset(key, 0, sizeof(*key));
  key->index = (uint32_t)key_idx;
  key->flags = BRCMF_PRIMARY_KEY;

  /* Clear the key/index */
  err = send_key_to_dongle(ifp, key);

  BRCMF_DBG(TRACE, "Exit");
  return err;
}

static zx_status_t brcmf_cfg80211_add_key(struct net_device* ndev,
                                          const set_key_descriptor_t* req) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_wsec_key* key;
  int32_t val;
  int32_t wsec;
  zx_status_t err;
  bool ext_key;
  uint8_t key_idx = req->key_id;
  const uint8_t* mac_addr = req->address;

  BRCMF_DBG(TRACE, "Enter");
  BRCMF_DBG(CONN, "key index (%d)", key_idx);
  if (!check_vif_up(ifp->vif)) {
    return ZX_ERR_IO;
  }

  if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
    /* we ignore this key index in this case */
    BRCMF_ERR("invalid key index (%d)", key_idx);
    return ZX_ERR_INVALID_ARGS;
  }

  if (req->key_count == 0) {
    return brcmf_cfg80211_del_key(ndev, key_idx);
  }

  if (req->key_count > sizeof(key->data)) {
    BRCMF_ERR("Too long key length (%zu)", req->key_count);
    return ZX_ERR_INVALID_ARGS;
  }

  ext_key = false;
  if (mac_addr && !address_is_multicast(mac_addr) &&
      (req->cipher_suite_type != WPA_CIPHER_WEP_40) &&
      (req->cipher_suite_type != WPA_CIPHER_WEP_104)) {
    BRCMF_DBG(TRACE, "Ext key, mac " FMT_MAC, FMT_MAC_ARGS(mac_addr));
    ext_key = true;
  }

  key = &ifp->vif->profile.key[key_idx];
  memset(key, 0, sizeof(*key));
  if ((ext_key) && (!address_is_multicast(mac_addr))) {
    memcpy((char*)&key->ea, (void*)mac_addr, ETH_ALEN);
  }
  key->len = req->key_count;
  key->index = key_idx;
  memcpy(key->data, req->key_list, key->len);
  if (!ext_key) {
    key->flags = BRCMF_PRIMARY_KEY;
  }

  switch (req->cipher_suite_type) {
    case WPA_CIPHER_WEP_40:
      key->algo = CRYPTO_ALGO_WEP1;
      val = WEP_ENABLED;
      BRCMF_DBG(CONN, "WPA_CIPHER_WEP_40");
      break;
    case WPA_CIPHER_WEP_104:
      key->algo = CRYPTO_ALGO_WEP128;
      val = WEP_ENABLED;
      BRCMF_DBG(CONN, "WPA_CIPHER_WEP_104");
      break;
    case WPA_CIPHER_TKIP:
      /* Note: Linux swaps the Tx and Rx MICs in client mode, but this doesn't work for us (see
         fxbug.dev/28642). It's unclear why this would be necessary. */
      key->algo = CRYPTO_ALGO_TKIP;
      val = TKIP_ENABLED;
      BRCMF_DBG(CONN, "WPA_CIPHER_TKIP");
      break;
    case WPA_CIPHER_CMAC_128:
      key->algo = CRYPTO_ALGO_AES_CCM;
      val = AES_ENABLED;
      BRCMF_DBG(CONN, "WPA_CIPHER_CMAC_128");
      break;
    case WPA_CIPHER_CCMP_128:
      key->algo = CRYPTO_ALGO_AES_CCM;
      val = AES_ENABLED;
      BRCMF_DBG(CONN, "WPA_CIPHER_CCMP_128");
      break;
    default:
      BRCMF_ERR("Unsupported cipher (0x%x)", req->cipher_suite_type);
      err = ZX_ERR_INVALID_ARGS;
      goto done;
  }

  BRCMF_DBG(CONN, "key length (%d) key index (%d) algo (%d) flags (%d)", key->len, key->index,
            key->algo, key->flags);
  err = send_key_to_dongle(ifp, key);
  if (err != ZX_OK) {
    goto done;
  }

  if (ext_key) {
    goto done;
  }
  err = brcmf_fil_bsscfg_int_get(ifp, "wsec", (uint32_t*)&wsec);  // TODO(cphoenix): This cast?!?
  if (err != ZX_OK) {
    BRCMF_ERR("get wsec error (%d)", err);
    goto done;
  }
  wsec |= val;
  BRCMF_DBG(CONN, "setting wsec to 0x%x", wsec);
  err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
  if (err != ZX_OK) {
    BRCMF_ERR("set wsec error (%d)", err);
    goto done;
  }

done:
  BRCMF_DBG(TRACE, "Exit");
  return err;
}

// EAPOL frames are queued up along with event notifications to ensure processing order.
void brcmf_cfg80211_handle_eapol_frame(struct brcmf_if* ifp, const void* data, size_t size) {
  struct net_device* ndev = ifp->ndev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping eapol frame callback");
    return;
  }
  const char* const data_bytes = reinterpret_cast<const char*>(data);
  wlan_fullmac_eapol_indication_t eapol_ind;
  // IEEE Std. 802.1X-2010, 11.3, Figure 11-1
  memcpy(&eapol_ind.dst_addr, data_bytes, ETH_ALEN);
  memcpy(&eapol_ind.src_addr, data_bytes + 6, ETH_ALEN);
  eapol_ind.data_count = size - 14;
  eapol_ind.data_list = reinterpret_cast<const uint8_t*>(data_bytes + 14);

  BRCMF_IFDBG(WLANIF, ndev, "Sending EAPOL frame to SME. data_len: %zu", eapol_ind.data_count);

  wlan_fullmac_impl_ifc_eapol_ind(&ndev->if_proto, &eapol_ind);
}

#define EAPOL_ETHERNET_TYPE_UINT16 0x8e88
void brcmf_cfg80211_rx(struct brcmf_if* ifp, const void* data, size_t size) {
  struct net_device* ndev = ifp->ndev;

  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping data recv");
    return;
  }
  BRCMF_THROTTLE_IF(5, BRCMF_IS_ON(BYTES) && BRCMF_IS_ON(DATA),
                    BRCMF_DBG_HEX_DUMP(true, data, std::min<size_t>(size, 64u),
                                       "Data received (%zu bytes, max 64 shown):", size));
  // IEEE Std. 802.3-2015, 3.1.1
  const uint16_t eth_type = ((uint16_t*)(data))[6];
  if (eth_type == EAPOL_ETHERNET_TYPE_UINT16) {
    // queue up the eapol frame along with events to ensure processing order
    brcmf_fweh_queue_eapol_frame(ifp, data, size);
  } else {
    wlan_fullmac_impl_ifc_data_recv(&ndev->if_proto, reinterpret_cast<const uint8_t*>(data), size,
                                    0);
  }
}

static bool brcmf_is_eapol_frame(const wlan::drivers::components::Frame& frame) {
  if (frame.Size() >= sizeof(ethhdr)) {
    const uint16_t eth_type = reinterpret_cast<const uint16_t*>(frame.Data())[6];
    return eth_type == EAPOL_ETHERNET_TYPE_UINT16;
  }
  return false;
}

void brcmf_cfg80211_rx(struct brcmf_if* ifp, wlan::drivers::components::Frame&& frame) {
  // First check if this is an EAPOL frame, if it is it should not go back to the network device.
  if (brcmf_is_eapol_frame(frame)) {
    // Queue up the eapol frame along with events to ensure processing order.
    brcmf_fweh_queue_eapol_frame(ifp, frame.Data(), frame.Size());
    return;
  }

  ifp->drvr->device->NetDev().CompleteRx(std::move(frame));
}

void brcmf_cfg80211_rx(struct brcmf_pub* drvr, wlan::drivers::components::FrameContainer&& frames) {
  // Make sure we process and exclude any EAPOL frames first
  for (auto& frame : frames) {
    if (brcmf_is_eapol_frame(frame)) {
      // queue up the eapol frame along with events to ensure processing order
      brcmf_fweh_queue_eapol_frame(brcmf_get_ifp(drvr, frame.PortId()), frame.Data(), frame.Size());
      // The data plane shouldn't do anything with the EAPOL data so set the size to zero here to
      // ensure that it's not processed after this point. The frame will still be passed around, the
      // data just won't be accessed because if it's zero in size there is technically  no data.
      frame.SetSize(0);
    }
  }

  drvr->device->NetDev().CompleteRx(std::move(frames));
}

static void brcmf_iedump(uint8_t* ies, size_t total_len) {
  if (BRCMF_IS_ON(CONN) && BRCMF_IS_ON(BYTES)) {
    size_t offset = 0;
    while (offset + TLV_HDR_LEN <= total_len) {
      uint8_t elem_type = ies[offset];
      uint8_t elem_len = ies[offset + TLV_LEN_OFF];
      offset += TLV_HDR_LEN;
      if (offset + elem_len > total_len) {
        break;
      }
      BRCMF_DBG_HEX_DUMP(true, ies + offset, elem_len, "IE %d, len %d:", elem_type, elem_len);
      offset += elem_len;
    }
    if (offset != total_len) {
      BRCMF_DBG(ALL, " * * Offset %ld didn't match length %ld", offset, total_len);
    }
  }
}

static void brcmf_return_scan_result(struct net_device* ndev, uint16_t channel,
                                     const uint8_t* bssid, uint16_t capability, uint16_t interval,
                                     uint8_t* ie, size_t ie_len, int16_t rssi_dbm) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  struct brcmf_cfg80211_info* cfg = ndev_to_if(ndev)->drvr->config;
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping scan result callback");
    return;
  }
  if (!brcmf_test_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status)) {
    return;
  }
  wlan_fullmac_scan_result_t result = {};

  result.txn_id = ndev->scan_txn_id;
  result.timestamp_nanos = zx::clock::get_monotonic().get();
  memcpy(result.bss.bssid, bssid, ETH_ALEN);
  // TODO(fxbug.dev/80230): The probably shouldn't be hardcoded.
  result.bss.bss_type = BSS_TYPE_INFRASTRUCTURE;
  result.bss.beacon_period = 0;
  result.bss.capability_info = capability;
  result.bss.channel.primary = (uint8_t)channel;
  // TODO(fxbug.dev/80231): This probably shouldn't be hardcoded.
  result.bss.channel.cbw = CHANNEL_BANDWIDTH_CBW20;
  result.bss.rssi_dbm = std::min<int16_t>(0, std::max<int16_t>(-255, rssi_dbm));
  result.bss.ies_list = ie;
  result.bss.ies_count = ie_len;

  auto ssid = brcmf_find_ssid_in_ies(result.bss.ies_list, result.bss.ies_count);

  BRCMF_DBG(SCAN, "Returning scan result id: %lu, channel: %d, dbm: %d", result.txn_id, channel,
            result.bss.rssi_dbm);
#if !defined(NDEBUG)
  BRCMF_DBG(SCAN, "  ssid: " FMT_SSID, FMT_SSID_VECT(ssid));
#endif /* !defined(NDEBUG) */
  ndev->scan_num_results++;
  wlan_fullmac_impl_ifc_on_scan_result(&ndev->if_proto, &result);
}

static zx_status_t brcmf_inform_single_bss(struct net_device* ndev, struct brcmf_cfg80211_info* cfg,
                                           struct brcmf_bss_info_le* bi) {
  struct brcmu_chan ch;
  uint16_t channel;
  uint16_t notify_capability;
  uint16_t notify_interval;
  uint8_t* notify_ie;
  size_t notify_ielen;
  int16_t notify_rssi_dbm;

  if (bi->length > WL_BSS_INFO_MAX) {
    BRCMF_ERR("Bss info is larger than buffer. Discarding");
    BRCMF_DBG(TEMP, "Early return, due to length.");
    return ZX_OK;
  }

  if (!bi->ctl_ch) {
    ch.chspec = bi->chanspec;
    cfg->d11inf.decchspec(&ch);
    bi->ctl_ch = ch.control_ch_num;
  }
  channel = bi->ctl_ch;

  notify_capability = bi->capability;
  notify_interval = bi->beacon_period;
  notify_ie = (uint8_t*)bi + bi->ie_offset;
  notify_ielen = bi->ie_length;
  notify_rssi_dbm = (int16_t)bi->RSSI;

  BRCMF_DBG(CONN,
            "Scan result received  BSS: " FMT_MAC
            "  Channel: %3d  Capability: %#6x  Beacon interval: %5d  Signal: %4d",
            FMT_MAC_ARGS(bi->BSSID), channel, notify_capability, notify_interval, notify_rssi_dbm);
  if (BRCMF_IS_ON(CONN) && BRCMF_IS_ON(BYTES)) {
    brcmf_iedump(notify_ie, notify_ielen);
  }

  brcmf_return_scan_result(ndev, (uint8_t)channel, (const uint8_t*)bi->BSSID, notify_capability,
                           notify_interval, notify_ie, notify_ielen, notify_rssi_dbm);

  return ZX_OK;
}

static zx_status_t brcmf_abort_scanning(struct brcmf_cfg80211_info* cfg) {
  struct escan_info* escan = &cfg->escan_info;
  zx_status_t err = ZX_OK;

  if (brcmf_test_and_set_bit(brcmf_scan_status_bit_t::ABORT, &cfg->scan_status)) {
    BRCMF_INFO("Abort scan already in progress.");
    return ZX_OK;
  }

  if (cfg->scan_in_progress) {
    escan->escan_state = WL_ESCAN_STATE_IDLE;
    if ((err = brcmf_abort_escan(escan->ifp)) != ZX_OK) {
      BRCMF_ERR("Abort scan failed -- error: %s", zx_status_get_string(err));
    }
  }
  brcmf_clear_bit(brcmf_scan_status_bit_t::ABORT, &cfg->scan_status);
  return err;
}

// Abort scanning immediately and inform SME right away
static void brcmf_abort_scanning_immediately(struct brcmf_cfg80211_info* cfg) {
  brcmf_abort_scanning(cfg);
  if (cfg->scan_in_progress) {
    brcmf_notify_escan_complete(cfg, cfg->escan_info.ifp, BRCMF_E_STATUS_ABORT);
  }
}

static void brcmf_cfg80211_escan_timeout_worker(WorkItem* work) {
  struct brcmf_cfg80211_info* cfg =
      containerof(work, struct brcmf_cfg80211_info, escan_timeout_work);

  BRCMF_WARN("Scan timed out, sending notification of aborted scan");
  brcmf_abort_scanning_immediately(cfg);
}

static void brcmf_escan_timeout(struct brcmf_cfg80211_info* cfg) {
  cfg->pub->irq_callback_lock.lock();

  if (cfg->scan_in_progress) {
    BRCMF_ERR("scan timer expired");
    // If it's for SIM tests, won't enqueue.
    EXEC_TIMEOUT_WORKER(escan_timeout_work);
  }
  cfg->pub->irq_callback_lock.unlock();
}

static zx_status_t brcmf_cfg80211_is_valid_sync_id(net_device* ndev,
                                                   const brcmf_escan_result_le* result,
                                                   uint32_t size) {
  std::lock_guard sync_id_lock(ndev->scan_sync_id_mutex);
  if (size < sizeof(result->sync_id) + offsetof(brcmf_escan_result_le, sync_id)) {
    BRCMF_ERR("Invalid escan result, not enough data in result, %u available", size);
    return false;
  }
  if (result->sync_id != ndev->scan_sync_id) {
    BRCMF_ERR("Invalid escan result with sync_id %u, current scan_sync_id %u", result->sync_id,
              ndev->scan_sync_id);
    return false;
  }
  return true;
}

static zx_status_t brcmf_cfg80211_escan_handler(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct net_device* ndev = cfg_to_ndev(cfg);
  brcmf_fweh_event_status_t status = e->status;
  uint32_t escan_buflen;
  struct brcmf_bss_info_le* bss_info_le;
  auto escan_result_le = static_cast<struct brcmf_escan_result_le*>(data);

  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  if (!escan_result_le) {
    BRCMF_ERR("Invalid escan result (nullptr)");
    goto chk_scan_end;
  }

  if (!brcmf_cfg80211_is_valid_sync_id(ndev, escan_result_le, e->datalen)) {
    return ZX_ERR_UNAVAILABLE;
  }

  if (status == BRCMF_E_STATUS_ABORT) {
    BRCMF_INFO("Firmware aborted escan: %d", e->reason);
    goto chk_scan_end;
  }

  if (!brcmf_test_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status)) {
    BRCMF_ERR("scan not ready, bsscfgidx=%d", ifp->bsscfgidx);
    return ZX_ERR_UNAVAILABLE;
  }

  bss_info_le = &escan_result_le->bss_info_le;

  if (e->datalen < sizeof(*escan_result_le)) {
    // Print the error only if the scan result is partial (as end of scan may not
    // contain a scan result)
    if (status == BRCMF_E_STATUS_PARTIAL) {
      BRCMF_ERR("Insufficient escan result data exp: %lu got: %d", sizeof(*escan_result_le),
                e->datalen);
    }
    goto chk_scan_end;
  }

  escan_buflen = escan_result_le->buflen;
  if (escan_buflen > BRCMF_ESCAN_BUF_SIZE || escan_buflen > e->datalen ||
      escan_buflen < sizeof(*escan_result_le)) {
    BRCMF_ERR("Invalid escan buffer length: %d", escan_buflen);
    goto chk_scan_end;
  }

  if (escan_result_le->bss_count != 1) {
    BRCMF_ERR("Invalid bss_count %d: ignoring", escan_result_le->bss_count);
    goto chk_scan_end;
  }

  if (!cfg->scan_in_progress) {
    BRCMF_DBG(SCAN, "result without cfg80211 request");
    goto chk_scan_end;
  }

  if (bss_info_le->length != escan_buflen - WL_ESCAN_RESULTS_FIXED_SIZE) {
    BRCMF_ERR("Ignoring invalid bss_info length: %d", bss_info_le->length);
    goto chk_scan_end;
  }

  brcmf_inform_single_bss(ndev, cfg, bss_info_le);

  if (status == BRCMF_E_STATUS_PARTIAL) {
    BRCMF_DBG(SCAN, "ESCAN Partial result");
    goto done;
  }

chk_scan_end:
  // If this is not a partial notification, indicate scan complete to wlanstack
  if (status != BRCMF_E_STATUS_PARTIAL) {
    cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
    if (cfg->scan_in_progress) {
      brcmf_notify_escan_complete(cfg, ifp, status);
    } else {
      BRCMF_DBG(SCAN, "Ignored scan complete result 0x%x", status);
    }
  }

done:
  return ZX_OK;
}

static void brcmf_init_escan(struct brcmf_cfg80211_info* cfg) {
  brcmf_fweh_register(cfg->pub, BRCMF_E_ESCAN_RESULT, brcmf_cfg80211_escan_handler);
  cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
  /* Init scan_timeout timer */
  cfg->escan_timer =
      new Timer(cfg->pub->device->GetDispatcher(), std::bind(brcmf_escan_timeout, cfg), false);
  cfg->escan_timeout_work = WorkItem(brcmf_cfg80211_escan_timeout_worker);
}

static zx_status_t brcmf_parse_vndr_ies(const uint8_t* vndr_ie_buf, uint32_t vndr_ie_len,
                                        struct parsed_vndr_ies* vndr_ies) {
  struct brcmf_vs_tlv* vndrie;
  struct brcmf_tlv* ie;
  struct parsed_vndr_ie_info* parsed_info;
  int32_t remaining_len;

  remaining_len = (int32_t)vndr_ie_len;
  memset(vndr_ies, 0, sizeof(*vndr_ies));

  ie = (struct brcmf_tlv*)vndr_ie_buf;
  while (ie) {
    if (ie->id != WLAN_IE_TYPE_VENDOR_SPECIFIC) {
      goto next;
    }
    vndrie = (struct brcmf_vs_tlv*)ie;
    /* len should be bigger than OUI length + one */
    if (vndrie->len < (VS_IE_FIXED_HDR_LEN - TLV_HDR_LEN + 1)) {
      BRCMF_ERR("invalid vndr ie. length is too small %d", vndrie->len);
      goto next;
    }
    /* if wpa or wme ie, do not add ie */
    if (!memcmp(vndrie->oui, (uint8_t*)MSFT_OUI, TLV_OUI_LEN) &&
        ((vndrie->oui_type == WPA_OUI_TYPE) || (vndrie->oui_type == WME_OUI_TYPE))) {
      BRCMF_DBG(TRACE, "Found WPA/WME oui. Do not add it");
      goto next;
    }

    parsed_info = &vndr_ies->ie_info[vndr_ies->count];

    /* save vndr ie information */
    parsed_info->ie_ptr = (uint8_t*)vndrie;
    parsed_info->ie_len = vndrie->len + TLV_HDR_LEN;
    memcpy(&parsed_info->vndrie, vndrie, sizeof(*vndrie));

    vndr_ies->count++;

    BRCMF_DBG(TRACE, "** OUI %02x %02x %02x, type 0x%02x", parsed_info->vndrie.oui[0],
              parsed_info->vndrie.oui[1], parsed_info->vndrie.oui[2], parsed_info->vndrie.oui_type);

    if (vndr_ies->count >= VNDR_IE_PARSE_LIMIT) {
      break;
    }
  next:
    remaining_len -= (ie->len + TLV_HDR_LEN);
    if (remaining_len <= TLV_HDR_LEN) {
      ie = nullptr;
    } else {
      ie = (struct brcmf_tlv*)(((uint8_t*)ie) + ie->len + TLV_HDR_LEN);
    }
  }
  return ZX_OK;
}

static uint32_t brcmf_vndr_ie(uint8_t* iebuf, int32_t pktflag, uint8_t* ie_ptr, uint32_t ie_len,
                              int8_t* add_del_cmd) {
  strncpy((char*)iebuf, (char*)add_del_cmd, VNDR_IE_CMD_LEN - 1);
  iebuf[VNDR_IE_CMD_LEN - 1] = '\0';

  *(uint32_t*)&iebuf[VNDR_IE_COUNT_OFFSET] = 1;

  *(uint32_t*)&iebuf[VNDR_IE_PKTFLAG_OFFSET] = pktflag;

  memcpy(&iebuf[VNDR_IE_VSIE_OFFSET], ie_ptr, ie_len);

  return ie_len + VNDR_IE_HDR_SIZE;
}

zx_status_t brcmf_vif_set_mgmt_ie(struct brcmf_cfg80211_vif* vif, int32_t pktflag,
                                  const uint8_t* vndr_ie_buf, uint32_t vndr_ie_len) {
  struct brcmf_if* ifp;
  struct vif_saved_ie* saved_ie;
  zx_status_t err = ZX_OK;
  uint8_t* iovar_ie_buf;
  uint8_t* curr_ie_buf;
  uint8_t* mgmt_ie_buf = nullptr;
  int mgmt_ie_buf_len;
  uint32_t* mgmt_ie_len;
  uint32_t del_add_ie_buf_len = 0;
  uint32_t total_ie_buf_len = 0;
  uint32_t parsed_ie_buf_len = 0;
  struct parsed_vndr_ies old_vndr_ies;
  struct parsed_vndr_ies new_vndr_ies;
  struct parsed_vndr_ie_info* vndrie_info;
  int32_t i;
  uint8_t* ptr;
  int remained_buf_len;

  if (!vif) {
    return ZX_ERR_IO_NOT_PRESENT;
  }
  ifp = vif->ifp;
  saved_ie = &vif->saved_ie;

  BRCMF_DBG(TRACE, "bsscfgidx %d, pktflag : 0x%02X", ifp->bsscfgidx, pktflag);
  iovar_ie_buf = static_cast<decltype(iovar_ie_buf)>(calloc(1, WL_EXTRA_BUF_MAX));
  if (!iovar_ie_buf) {
    return ZX_ERR_NO_MEMORY;
  }
  curr_ie_buf = iovar_ie_buf;
  switch (pktflag) {
    case BRCMF_VNDR_IE_PRBREQ_FLAG:
      mgmt_ie_buf = saved_ie->probe_req_ie;
      mgmt_ie_len = &saved_ie->probe_req_ie_len;
      mgmt_ie_buf_len = sizeof(saved_ie->probe_req_ie);
      break;
    case BRCMF_VNDR_IE_PRBRSP_FLAG:
      mgmt_ie_buf = saved_ie->probe_res_ie;
      mgmt_ie_len = &saved_ie->probe_res_ie_len;
      mgmt_ie_buf_len = sizeof(saved_ie->probe_res_ie);
      break;
    case BRCMF_VNDR_IE_BEACON_FLAG:
      mgmt_ie_buf = saved_ie->beacon_ie;
      mgmt_ie_len = &saved_ie->beacon_ie_len;
      mgmt_ie_buf_len = sizeof(saved_ie->beacon_ie);
      break;
    case BRCMF_VNDR_IE_ASSOCREQ_FLAG:
      mgmt_ie_buf = saved_ie->assoc_req_ie;
      mgmt_ie_len = &saved_ie->assoc_req_ie_len;
      mgmt_ie_buf_len = sizeof(saved_ie->assoc_req_ie);
      break;
    default:
      err = ZX_ERR_WRONG_TYPE;
      BRCMF_ERR("not suitable type");
      goto exit;
  }

  if ((int)vndr_ie_len > mgmt_ie_buf_len) {
    err = ZX_ERR_NO_MEMORY;
    BRCMF_ERR("extra IE size too big");
    goto exit;
  }

  /* parse and save new vndr_ie in curr_ie_buff before comparing it */
  if (vndr_ie_buf && vndr_ie_len && curr_ie_buf) {
    ptr = curr_ie_buf;
    brcmf_parse_vndr_ies(vndr_ie_buf, vndr_ie_len, &new_vndr_ies);
    for (i = 0; i < (int32_t)new_vndr_ies.count; i++) {
      vndrie_info = &new_vndr_ies.ie_info[i];
      memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr, vndrie_info->ie_len);
      parsed_ie_buf_len += vndrie_info->ie_len;
    }
  }

  if (mgmt_ie_buf && *mgmt_ie_len) {
    if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
        (memcmp(mgmt_ie_buf, curr_ie_buf, parsed_ie_buf_len) == 0)) {
      BRCMF_DBG(TRACE, "Previous mgmt IE equals to current IE");
      goto exit;
    }

    /* parse old vndr_ie */
    brcmf_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len, &old_vndr_ies);

    /* make a command to delete old ie */
    for (i = 0; i < (int32_t)old_vndr_ies.count; i++) {
      vndrie_info = &old_vndr_ies.ie_info[i];

      BRCMF_DBG(TRACE, "DEL ID : %d, Len: %d , OUI:%02x:%02x:%02x", vndrie_info->vndrie.id,
                vndrie_info->vndrie.len, vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
                vndrie_info->vndrie.oui[2]);

      del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag, vndrie_info->ie_ptr,
                                         vndrie_info->ie_len, (int8_t*)"del");
      curr_ie_buf += del_add_ie_buf_len;
      total_ie_buf_len += del_add_ie_buf_len;
    }
  }

  *mgmt_ie_len = 0;
  /* Add if there is any extra IE */
  if (mgmt_ie_buf && parsed_ie_buf_len) {
    ptr = mgmt_ie_buf;

    remained_buf_len = mgmt_ie_buf_len;

    /* make a command to add new ie */
    for (i = 0; i < (int32_t)new_vndr_ies.count; i++) {
      vndrie_info = &new_vndr_ies.ie_info[i];

      /* verify remained buf size before copy data */
      if (remained_buf_len < (vndrie_info->vndrie.len + VNDR_IE_VSIE_OFFSET)) {
        BRCMF_ERR("no space in mgmt_ie_buf: len left %d", remained_buf_len);
        break;
      }
      remained_buf_len -= (vndrie_info->ie_len + VNDR_IE_VSIE_OFFSET);

      BRCMF_DBG(TRACE, "ADDED ID : %d, Len: %d, OUI:%02x:%02x:%02x", vndrie_info->vndrie.id,
                vndrie_info->vndrie.len, vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
                vndrie_info->vndrie.oui[2]);

      del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag, vndrie_info->ie_ptr,
                                         vndrie_info->ie_len, (int8_t*)"add");

      /* save the parsed IE in wl struct */
      memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr, vndrie_info->ie_len);
      *mgmt_ie_len += vndrie_info->ie_len;

      curr_ie_buf += del_add_ie_buf_len;
      total_ie_buf_len += del_add_ie_buf_len;
    }
  }
  if (total_ie_buf_len) {
    err = brcmf_fil_bsscfg_data_set(ifp, "vndr_ie", iovar_ie_buf, total_ie_buf_len);
    if (err != ZX_OK) {
      BRCMF_ERR("vndr ie set error : %d", err);
    }
  }

exit:
  free(iovar_ie_buf);
  return err;
}

zx_status_t brcmf_vif_clear_mgmt_ies(struct brcmf_cfg80211_vif* vif) {
  int32_t pktflags[] = {BRCMF_VNDR_IE_PRBREQ_FLAG, BRCMF_VNDR_IE_PRBRSP_FLAG,
                        BRCMF_VNDR_IE_BEACON_FLAG};
  int i;

  for (i = 0; i < (int)std::size(pktflags); i++) {
    brcmf_vif_set_mgmt_ie(vif, pktflags[i], nullptr, 0);
  }

  memset(&vif->saved_ie, 0, sizeof(vif->saved_ie));
  return ZX_OK;
}

bool brcmf_is_ap_start_pending(brcmf_cfg80211_info* cfg) {
  struct net_device* softap_ndev = cfg_to_softap_ndev(cfg);

  // No softAP interface
  if (softap_ndev == nullptr) {
    return false;
  }

  struct brcmf_cfg80211_vif* vif = ndev_to_vif(softap_ndev);
  return brcmf_test_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &vif->sme_state);
}

// Deauthenticate with specified STA.
static wlan_stop_result_t brcmf_cfg80211_stop_ap(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  zx_status_t status;
  bcme_status_t fw_err = BCME_OK;
  wlan_stop_result_t result = WLAN_STOP_RESULT_SUCCESS;
  struct brcmf_join_params join_params;
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  if (!brcmf_test_bit(brcmf_vif_status_bit_t::AP_CREATED, &ifp->vif->sme_state) &&
      !brcmf_test_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state)) {
    BRCMF_INFO("attempt to stop already stopped AP");
    return WLAN_STOP_RESULT_BSS_ALREADY_STOPPED;
  }

  // If we are in the process of resetting, then ap interface no longer exists
  // in firmware (since fw has been reloaded). We can skip sending commands
  // related to destorying the interface.
  if (ifp->drvr->drvr_resetting.load()) {
    goto skip_fw_cmds;
  }

  memset(&join_params, 0, sizeof(join_params));
  status =
      brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID, &join_params, sizeof(join_params), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("SET SSID error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    result = WLAN_STOP_RESULT_INTERNAL_ERROR;
  }

  // Issue "bss" iovar to bring down the SoftAP IF.
  brcmf_bss_ctrl bss_down;
  bss_down.bsscfgidx = ifp->bsscfgidx;
  bss_down.value = 0;
  status = brcmf_fil_bsscfg_data_set(ifp, "bss", &bss_down, sizeof(bss_down));
  if (status != ZX_OK) {
    // If "bss" fails, issue C_DOWN/UP to cleanly shutdown the SoftAP
    status = brcmf_bss_reset(ifp);
    if (status != ZX_OK) {
      BRCMF_ERR("BRCMF_C_DOWN/UP error: %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
    }
  }

  // Disable AP mode in MFG build since the IF is shared.
  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 0, &fw_err);
    if (status != ZX_OK) {
      BRCMF_ERR("Unset AP mode failed %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
    }
  }
  brcmf_vif_clear_mgmt_ies(ifp->vif);

  // ap_started must be unset for brcmf_enable_mpc() to take effect.
  cfg->ap_started = false;
  brcmf_enable_mpc(ifp, 1);

skip_fw_cmds:
  cfg->ap_started = false;
  brcmf_clear_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::AP_CREATED, &ifp->vif->sme_state);
  brcmf_net_setcarrier(ifp, false);

  return result;
}

// Returns an MLME result code (WLAN_START_RESULT_*) if an error is encountered.
// If all iovars succeed, MLME is notified when E_LINK event is received.
static uint8_t brcmf_cfg80211_start_ap(struct net_device* ndev,
                                       const wlan_fullmac_start_req_t* req) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  if (brcmf_test_bit(brcmf_vif_status_bit_t::AP_CREATED, &ifp->vif->sme_state)) {
    BRCMF_ERR("AP already started");
    return WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED;
  }

  if (brcmf_test_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state)) {
    BRCMF_ERR("AP start request received, start pending");
    return WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED;
  }

  if (req->bss_type != BSS_TYPE_INFRASTRUCTURE) {
    BRCMF_ERR("Attempt to start AP in unsupported mode (%d)", req->bss_type);
    return WLAN_START_RESULT_NOT_SUPPORTED;
  }

  if (ifp->vif->mbss) {
    BRCMF_ERR("Mesh role not yet supported");
    return WLAN_START_RESULT_NOT_SUPPORTED;
  }

  // Enter AP_START_PENDING mode before we abort any on-going scans. As soon as
  // we abort a scan we're open for other scans coming in and we want to make
  // sure those scans are blocked by setting this bit.
  brcmf_set_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state);

  if (brcmf_test_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status)) {
    std::string scan_status_report;
    brcmf_check_scan_status(cfg->scan_status.load(), &scan_status_report);
    BRCMF_INFO("AP start request incoming during scan_status %s", scan_status_report.c_str());
    BRCMF_INFO("Aborting scan to continue AP start request.");
    brcmf_abort_scanning(cfg);
  }

  BRCMF_DBG(TRACE,
            "ssid: " FMT_SSID "  beacon period: %d  dtim_period: %d  channel: %d  rsne_len: %zd",
            FMT_SSID_BYTES(req->ssid.data, req->ssid.len), req->beacon_period, req->dtim_period,
            req->channel, req->rsne_len);

  wlan_channel_t channel = {};
  uint16_t chanspec = 0;
  zx_status_t status;
  bcme_status_t fw_err = BCME_OK;

  struct brcmf_ssid_le ssid_le;
  memset(&ssid_le, 0, sizeof(ssid_le));
  memcpy(ssid_le.SSID, req->ssid.data, req->ssid.len);
  ssid_le.SSID_len = req->ssid.len;

  brcmf_enable_mpc(ifp, 0);

  // Start timer before starting to issue commands.
  cfg->ap_start_timer->Start(BRCMF_AP_START_TIMER_DUR_MS);
  // set to open authentication for external supplicant
  status = brcmf_fil_bsscfg_int_set(ifp, "auth", BRCMF_AUTH_MODE_OPEN);
  if (status != ZX_OK) {
    BRCMF_ERR("auth error %s", zx_status_get_string(status));
    goto fail;
  }

  // Configure RSN IE
  if (req->rsne_len != 0) {
    struct brcmf_vs_tlv* tmp_ie = (struct brcmf_vs_tlv*)req->rsne;
    status = brcmf_configure_wpaie(ifp, tmp_ie, true, true);
    if (status != ZX_OK) {
      BRCMF_ERR("Failed to install RSNE: %s", zx_status_get_string(status));
      goto fail;
    }
  } else {
    status = brcmf_configure_opensecurity(ifp);
    if (status != ZX_OK) {
      BRCMF_ERR("Failed to configure AP for open security: %s", zx_status_get_string(status));
      goto fail;
    }
  }

  status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_BCNPRD, req->beacon_period, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("Beacon Interval Set Error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    goto fail;
  }
  ifp->vif->profile.beacon_period = req->beacon_period;

  status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_DTIMPRD, req->dtim_period, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("DTIM Interval Set Error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    goto fail;
  }

  // If we are operating with manufacturing FW, we have access to just one IF
  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1, &fw_err);
    if (status != ZX_OK) {
      BRCMF_ERR("BRCMF_C_DOWN error %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
      goto fail;
    }
    // Disable simultaneous STA/AP operation
    status = brcmf_fil_iovar_int_set(ifp, "apsta", 0, &fw_err);
    if (status != ZX_OK) {
      BRCMF_ERR("Set apsta error %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
      goto fail;
    }
  }

  status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 1, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("SET INFRA error %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    goto fail;
  }

  status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 1, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("Set AP mode failed %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    goto fail;
  }

  channel = {.primary = req->channel, .cbw = CHANNEL_BANDWIDTH_CBW20, .secondary80 = 0};
  chanspec = channel_to_chanspec(&cfg->d11inf, &channel);
  status = brcmf_fil_iovar_int_set(ifp, "chanspec", chanspec, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("Set Channel failed: chspec=%d, status=%s, fw_err=%s", chanspec,
              zx_status_get_string(status), brcmf_fil_get_errstr(fw_err));
    goto fail;
  }

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1, &fw_err);
    if (status != ZX_OK) {
      BRCMF_ERR("BRCMF_C_UP error: %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
      goto fail;
    }
  }
  struct brcmf_join_params join_params;
  memset(&join_params, 0, sizeof(join_params));
  // join parameters starts with ssid
  memcpy(&join_params.ssid_le, &ssid_le, sizeof(ssid_le));
  // create softap
  status =
      brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID, &join_params, sizeof(join_params), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("SET SSID error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    if (fw_err == BCME_ERROR) {
      ifp->drvr->device->GetInspect()->LogApSetSsidErr();
    }
    goto fail;
  }

  BRCMF_DBG(TRACE, "AP mode configuration complete");

  brcmf_net_setcarrier(ifp, true);

  cfg->ap_started = true;
  return WLAN_START_RESULT_SUCCESS;

fail:
  // Stop the timer when the function fails to issue any of the commands.
  cfg->ap_start_timer->Stop();
  // Unconditionally stop the AP as some of the iovars might have succeeded and
  // thus the SoftAP might have been partially started.
  brcmf_cfg80211_stop_ap(ndev);

  return WLAN_START_RESULT_NOT_SUPPORTED;
}

static zx_status_t brcmf_cfg80211_del_station(struct net_device* ndev, const uint8_t* mac,
                                              wlan_ieee80211::ReasonCode reason) {
  BRCMF_DBG(TRACE, "Enter: reason: %d", static_cast<int>(reason));

  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_scb_val_le scbval;
  memset(&scbval, 0, sizeof(scbval));
  memcpy(&scbval.ea, mac, ETH_ALEN);
  scbval.val = static_cast<uint32_t>(reason);
  bcme_status_t fw_err = BCME_OK;
  zx_status_t status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
                                              sizeof(scbval), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("SCB_DEAUTHENTICATE_FOR_REASON failed: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
  }

  BRCMF_DBG(TRACE, "Exit");
  return status;
}

static zx_status_t brcmf_notify_tdls_peer_event(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
  switch (e->reason) {
    case BRCMF_E_REASON_TDLS_PEER_DISCOVERED:
      BRCMF_DBG(TRACE, "TDLS Peer Discovered");
      break;
    case BRCMF_E_REASON_TDLS_PEER_CONNECTED:
      BRCMF_DBG(TRACE, "TDLS Peer Connected");
      brcmf_proto_add_tdls_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
      break;
    case BRCMF_E_REASON_TDLS_PEER_DISCONNECTED:
      BRCMF_DBG(TRACE, "TDLS Peer Disconnected");
      brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
      break;
  }
  return ZX_OK;
}

// Country is initialized to US by default. This should be retrieved from location services
// when available.
zx_status_t brcmf_if_start(net_device* ndev, const wlan_fullmac_impl_ifc_protocol_t* ifc,
                           zx_handle_t* out_mlme_channel) {
  if (!ndev->mlme_channel.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  BRCMF_IFDBG(WLANIF, ndev, "Starting wlan_fullmac interface");
  {
    std::lock_guard<std::shared_mutex> guard(ndev->if_proto_lock);
    ndev->if_proto = *ifc;
  }
  brcmf_netdev_open(ndev);
  ndev->is_up = true;

  ZX_DEBUG_ASSERT(out_mlme_channel != nullptr);
  *out_mlme_channel = ndev->mlme_channel.release();
  return ZX_OK;
}

void brcmf_if_stop(net_device* ndev) {
  BRCMF_IFDBG(WLANIF, ndev, "Stopping wlan_fullmac interface");

  std::lock_guard<std::shared_mutex> guard(ndev->if_proto_lock);
  ndev->if_proto.ops = nullptr;
  ndev->if_proto.ctx = nullptr;
  ndev->is_up = false;
  BRCMF_IFDBG(WLANIF, ndev, "wlan_fullmac interface stopped");
}

void brcmf_if_start_scan(net_device* ndev, const wlan_fullmac_scan_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping scan request.");
    return;
  }
  zx_status_t result;

  BRCMF_IFDBG(WLANIF, ndev, "Scan request from SME. txn_id: %" PRIu64 ", type: %s", req->txn_id,
              req->scan_type == WLAN_SCAN_TYPE_PASSIVE  ? "passive"
              : req->scan_type == WLAN_SCAN_TYPE_ACTIVE ? "active"
                                                        : "invalid");

  ndev->scan_num_results = 0;

  uint16_t sync_id = 0;
  BRCMF_DBG(SCAN, "About to scan! Txn ID %lu", req->txn_id);
  // Lock here so that we ensure that the sync_id is stored in ndev before we start processing
  // results. Otherwise this thread might get suspended and results would come in and be discareded
  // because the sync_id would not match.
  std::lock_guard sync_id_lock(ndev->scan_sync_id_mutex);
  result = brcmf_cfg80211_scan(ndev, req, &sync_id);
  switch (result) {
    case ZX_OK:
      ndev->scan_txn_id = req->txn_id;
      ndev->scan_sync_id = sync_id;
      break;
    case ZX_ERR_SHOULD_WAIT:
      BRCMF_INFO("Scan failed. Firmware busy: %d %s", result, zx_status_get_string(result));
      brcmf_signal_scan_end(ndev, req->txn_id, WLAN_SCAN_RESULT_SHOULD_WAIT);
      break;
    case ZX_ERR_INVALID_ARGS:
      BRCMF_ERR("Scan failed. Invalid arguments: %d %s", result, zx_status_get_string(result));
      brcmf_signal_scan_end(ndev, req->txn_id, WLAN_SCAN_RESULT_INVALID_ARGS);
      break;
    default:
      BRCMF_INFO("Scan failed. Internal error: %d %s", result, zx_status_get_string(result));
      brcmf_signal_scan_end(ndev, req->txn_id, WLAN_SCAN_RESULT_INTERNAL_ERROR);
  }
}

void brcmf_if_connect_req(net_device* ndev, const wlan_fullmac_connect_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_profile* profile = &ifp->vif->profile;

  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping connect request");
    return;
  }

  wlan_fullmac_connect_confirm_t result = {};
  zx_status_t status;
  memcpy(result.peer_sta_address, req->selected_bss.bssid, sizeof(req->selected_bss.bssid));

  auto ssid = brcmf_find_ssid_in_ies(req->selected_bss.ies_list, req->selected_bss.ies_count);

  if (ssid.empty()) {
    BRCMF_DBG(WLANIF, "Connect request from SME exited: no SSID in request");
    result.result_code = STATUS_CODE_JOIN_FAILURE;
    goto fail;
  }

  if (req->wep_key.key_count > 0 && !(req->auth_type == WLAN_AUTH_TYPE_SHARED_KEY ||
                                      req->auth_type == WLAN_AUTH_TYPE_OPEN_SYSTEM)) {
    BRCMF_DBG(WLANIF, "Connect request from SME exited: unexpected WEP key in request");
    result.result_code = STATUS_CODE_JOIN_FAILURE;
    goto fail;
  }

  if (req->wep_key.key_count > MAX_SUPPORTED_WEP_KEY_LEN) {
    BRCMF_DBG(WLANIF, "Connect request from SME exited: WEP key len %zu larger than %d",
              req->wep_key.key_count, MAX_SUPPORTED_WEP_KEY_LEN);
    result.result_code = STATUS_CODE_JOIN_FAILURE;
    goto fail;
  }

  BRCMF_IFDBG(WLANIF, ndev, "Connect request from SME.");
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  ssid: " FMT_SSID ", bssid: " FMT_MAC ", channel: %u",
              FMT_SSID_VECT(ssid), FMT_MAC_ARGS(req->selected_bss.bssid),
              req->selected_bss.channel.primary);
#endif /* !defined(NDEBUG) */

  memcpy(&ifp->connect_req, req, sizeof(ifp->connect_req));
  if (ifp->connect_req.selected_bss.ies_count > fuchsia::wlan::ieee80211::WLAN_MSDU_MAX_LEN) {
    ifp->connect_req.selected_bss.ies_count = fuchsia::wlan::ieee80211::WLAN_MSDU_MAX_LEN;
  }
  // BSS IES, WEP key, and security IE pointers point to data we don't own,
  // so we have to copy them over.
  memcpy(&ifp->ies, ifp->connect_req.selected_bss.ies_list,
         ifp->connect_req.selected_bss.ies_count);
  ifp->connect_req.selected_bss.ies_list = ifp->ies;

  if (ifp->connect_req.wep_key.key_count > 0) {
    memcpy(&ifp->wep_key_bytes, ifp->connect_req.wep_key.key_list,
           ifp->connect_req.wep_key.key_count);
    ifp->connect_req.wep_key.key_list = ifp->wep_key_bytes;
  }

  if (ifp->connect_req.security_ie_count > fuchsia::wlan::ieee80211::WLAN_IE_MAX_LEN) {
    ifp->connect_req.security_ie_count = fuchsia::wlan::ieee80211::WLAN_IE_MAX_LEN;
  }
  memcpy(&ifp->security_ie, ifp->connect_req.security_ie_list, ifp->connect_req.security_ie_count);
  ifp->connect_req.security_ie_list = ifp->security_ie;

  memcpy(profile->bssid, req->selected_bss.bssid, ETH_ALEN);

  status = brcmf_configure_opensecurity(ifp);
  if (status != ZX_OK) {
    BRCMF_DBG(WLANIF, "Connect request from SME exited: unable to reset security iovars");
    result.result_code = STATUS_CODE_JOIN_FAILURE;
    goto fail;
  }

  if (ifp->connect_req.wep_key.key_count > 0) {
    auto add_key_result = brcmf_cfg80211_add_key(ndev, &ifp->connect_req.wep_key);
    if (add_key_result != ZX_OK) {
      BRCMF_DBG(WLANIF, "Connect request from SME exited: unable to set WEP key");
      result.result_code = STATUS_CODE_JOIN_FAILURE;
      goto fail;
    }
  }

  if (brcmf_set_auth_type(ndev, ifp->connect_req.auth_type) != ZX_OK) {
    BRCMF_IFDBG(WLANIF, ndev, "Connect request from SME exited: bad auth_type parameters");
    result.result_code = STATUS_CODE_UNSUPPORTED_AUTH_ALGORITHM;
    goto fail;
  }

  if (ifp->connect_req.security_ie_count > 0) {
    BRCMF_DBG(TEMP, " * * Security IE non-zero! %ld", ifp->connect_req.security_ie_count);
    BRCMF_DBG_HEX_DUMP(BRCMF_IS_ON(BYTES), ifp->connect_req.security_ie_list,
                       ifp->connect_req.security_ie_count, "Security IE:");
  }

  brcmf_cfg80211_connect(ndev, &ifp->connect_req);
  BRCMF_IFDBG(WLANIF, ndev, "Initiated connect request from SME");
  return;

fail:
  wlan_fullmac_impl_ifc_connect_conf(&ndev->if_proto, &result);
}

void brcmf_if_reconnect_req(net_device* ndev, const wlan_fullmac_reconnect_req_t* req) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  if (memcmp(req->peer_sta_address, ifp->connect_req.selected_bss.bssid, ETH_ALEN)) {
    BRCMF_ERR("Requested MAC != Connected MAC");
#if !defined(NDEBUG)
    const uint8_t* old_mac = ifp->connect_req.selected_bss.bssid;
    const uint8_t* new_mac = req->peer_sta_address;
    BRCMF_IFDBG(WLANIF, ndev, " requested mac: " FMT_MAC ", connected mac: " FMT_MAC,
                FMT_MAC_ARGS(new_mac), FMT_MAC_ARGS(old_mac));
#endif /* !defined(NDEBUG) */

    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
    return;
  }

  brcmf_cfg80211_connect(ndev, &ifp->connect_req);
}

// In AP mode, receive a response from wlan_fullmac confirming that a client was successfully
// authenticated.
void brcmf_if_auth_resp(net_device* ndev, const wlan_fullmac_auth_resp_t* ind) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  BRCMF_IFDBG(WLANIF, ndev, "Auth response from SME. result: %s",
              ind->result_code == WLAN_AUTH_RESULT_SUCCESS   ? "success"
              : ind->result_code == WLAN_AUTH_RESULT_REFUSED ? "refused"
              : ind->result_code == WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED
                  ? "anti-clogging token required"
              : ind->result_code == WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED
                  ? "finite cyclic group not supported"
              : ind->result_code == WLAN_AUTH_RESULT_REJECTED        ? "rejected"
              : ind->result_code == WLAN_AUTH_RESULT_FAILURE_TIMEOUT ? "timeout"
                                                                     : "invalid");
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  , address: " FMT_MAC, FMT_MAC_ARGS(ind->peer_sta_address));
#endif /* !defined(NDEBUG) */

  if (!brcmf_is_apmode(ifp->vif)) {
    BRCMF_ERR("Received AUTHENTICATE.response but not in AP mode - ignoring");
    return;
  }

  if (ind->result_code == WLAN_AUTH_RESULT_SUCCESS) {
    const uint8_t* mac = ind->peer_sta_address;
    BRCMF_DBG(CONN, "Successfully authenticated client " FMT_MAC "\n", FMT_MAC_ARGS(mac));
    return;
  }

  wlan_ieee80211::ReasonCode reason = {};
  switch (ind->result_code) {
    case WLAN_AUTH_RESULT_REFUSED:
    case WLAN_AUTH_RESULT_REJECTED:
      reason = wlan_ieee80211::ReasonCode::NOT_AUTHENTICATED;
      break;
    case WLAN_AUTH_RESULT_FAILURE_TIMEOUT:
      reason = wlan_ieee80211::ReasonCode::TIMEOUT;
      break;
    case WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED:
    case WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
    default:
      reason = wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
      break;
  }
  brcmf_cfg80211_del_station(ndev, ind->peer_sta_address, reason);
}

// Respond to a MLME-DEAUTHENTICATE.request message. Note that we are required to respond with a
// MLME-DEAUTHENTICATE.confirm on completion (or failure), even though there is no status
// reported.
void brcmf_if_deauth_req(net_device* ndev, const wlan_fullmac_deauth_req_t* req) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  BRCMF_IFDBG(WLANIF, ndev, "Deauth request from SME. reason: %" PRIu16 "", req->reason_code);

  if (brcmf_is_apmode(ifp->vif)) {
    struct brcmf_scb_val_le scbval;
    bcme_status_t fw_err = BCME_OK;

    memcpy(&scbval.ea, req->peer_sta_address, ETH_ALEN);
    scbval.val = req->reason_code;
    zx_status_t status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
                                                sizeof(scbval), &fw_err);
    if (status != ZX_OK) {
      BRCMF_ERR("Failed to disassociate: %s, fw err %s", zx_status_get_string(status),
                brcmf_fil_get_errstr(fw_err));
    }
    // Deauth confirm will get sent when the driver receives the DEAUTH_EVENT
    return;
  }

  // Client IF processing
  if (brcmf_cfg80211_disconnect(ndev, req->peer_sta_address, req->reason_code, true) != ZX_OK) {
    // Request to disconnect failed, so respond immediately
    brcmf_notify_deauth(ndev, req->peer_sta_address);
  }  // else wait for disconnect to complete before sending response

  // Workaround for fxbug.dev/28829: allow time for disconnect to complete
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
}

void brcmf_if_assoc_resp(net_device* ndev, const wlan_fullmac_assoc_resp_t* ind) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  BRCMF_IFDBG(WLANIF, ndev, "Assoc response from SME. result: %" PRIu8 ", aid: %" PRIu16,
              ind->result_code, ind->association_id);
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(ind->peer_sta_address));
#endif /* !defined(NDEBUG) */

  if (!brcmf_is_apmode(ifp->vif)) {
    BRCMF_ERR("Received ASSOCIATE.response but not in AP mode - ignoring");
    return;
  }

  if (ind->result_code == WLAN_ASSOC_RESULT_SUCCESS) {
    const uint8_t* mac = ind->peer_sta_address;
    BRCMF_DBG(CONN, "Successfully associated client " FMT_MAC, FMT_MAC_ARGS(mac));
    return;
  }

  // TODO(fxb/62115): The translation here is poor because the set of result codes
  // available for an association response is too small.
  wlan_ieee80211::ReasonCode reason = {};
  switch (ind->result_code) {
    case WLAN_ASSOC_RESULT_REFUSED_NOT_AUTHENTICATED:
      reason = wlan_ieee80211::ReasonCode::NOT_AUTHENTICATED;
      break;
    case WLAN_ASSOC_RESULT_REFUSED_CAPABILITIES_MISMATCH:
      reason = wlan_ieee80211::ReasonCode::INVALID_RSNE_CAPABILITIES;
      break;
    case WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED:
    case WLAN_ASSOC_RESULT_REFUSED_EXTERNAL_REASON:
    case WLAN_ASSOC_RESULT_REFUSED_AP_OUT_OF_MEMORY:
    case WLAN_ASSOC_RESULT_REFUSED_BASIC_RATES_MISMATCH:
    case WLAN_ASSOC_RESULT_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
    case WLAN_ASSOC_RESULT_REFUSED_TEMPORARILY:
    default:
      reason = wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON;
      break;
  }
  brcmf_cfg80211_del_station(ndev, ind->peer_sta_address, reason);
}

void brcmf_if_disassoc_req(net_device* ndev, const wlan_fullmac_disassoc_req_t* req) {
  BRCMF_IFDBG(WLANIF, ndev, "Disassoc request from SME. reason: %" PRIu16, req->reason_code);
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(req->peer_sta_address));
#endif /* !defined(NDEBUG) */
  zx_status_t status =
      brcmf_cfg80211_disconnect(ndev, req->peer_sta_address, req->reason_code, false);
  if (status != ZX_OK) {
    brcmf_notify_disassoc(ndev, status);
  }  // else notification will happen asynchronously
}

void brcmf_if_reset_req(net_device* ndev, const wlan_fullmac_reset_req_t* req) {
  BRCMF_IFDBG(WLANIF, ndev, "Reset request from SME.");
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC, FMT_MAC_ARGS(req->sta_address));
#endif /* !defined(NDEBUG) */

  BRCMF_ERR("Unimplemented");
}

void brcmf_if_start_conf(net_device* ndev, uint8_t result) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping AP start callback");
    return;
  }

  wlan_fullmac_start_confirm_t start_conf = {.result_code = result};
  BRCMF_IFDBG(WLANIF, ndev, "Sending AP start confirm to SME. result_code: %s",
              result == WLAN_START_RESULT_SUCCESS                         ? "success"
              : result == WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED ? "already started"
              : result == WLAN_START_RESULT_RESET_REQUIRED_BEFORE_START   ? "reset required"
              : result == WLAN_START_RESULT_NOT_SUPPORTED                 ? "not supported"
                                                                          : "unknown");

  wlan_fullmac_impl_ifc_start_conf(&ndev->if_proto, &start_conf);
}

// AP start timeout worker
static void brcmf_ap_start_timeout_worker(WorkItem* work) {
  struct brcmf_cfg80211_info* cfg =
      containerof(work, struct brcmf_cfg80211_info, ap_start_timeout_work);
  struct net_device* ndev = cfg_to_softap_ndev(cfg);
  struct brcmf_if* ifp = ndev_to_if(ndev);

  // Indicate status only if AP start pending is set
  if (brcmf_test_and_clear_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state)) {
    // Indicate AP start failed
    brcmf_if_start_conf(ndev, WLAN_START_RESULT_NOT_SUPPORTED);
  }
}

// AP start timeout handler
static void brcmf_ap_start_timeout(struct brcmf_cfg80211_info* cfg) {
  cfg->pub->irq_callback_lock.lock();
  BRCMF_DBG(TRACE, "Enter");
  EXEC_TIMEOUT_WORKER(ap_start_timeout_work);
  cfg->pub->irq_callback_lock.unlock();
}

/* Start AP mode */
void brcmf_if_start_req(net_device* ndev, const wlan_fullmac_start_req_t* req) {
  BRCMF_IFDBG(WLANIF, ndev, "Start AP request from SME. rsne_len: %zu, channel: %u", req->rsne_len,
              req->channel);
#if !defined(NDEBUG)
  BRCMF_DBG(WLANIF, "  ssid: " FMT_SSID, FMT_SSID_BYTES(req->ssid.data, req->ssid.len));
#endif /* !defined(NDEBUG) */

  uint8_t result_code = brcmf_cfg80211_start_ap(ndev, req);
  if (result_code != WLAN_START_RESULT_SUCCESS) {
    brcmf_if_start_conf(ndev, result_code);
  }
}

/* Stop AP mode */
void brcmf_if_stop_req(net_device* ndev, const wlan_fullmac_stop_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping AP stop callback");
    return;
  }

  BRCMF_IFDBG(WLANIF, ndev, "Stop AP request from SME.");
#if !defined(NDEBUG)
  BRCMF_DBG(WLANIF, "  ssid: " FMT_SSID, FMT_SSID_BYTES(req->ssid.data, req->ssid.len));
#endif /* !defined(NDEBUG) */

  uint8_t result_code = brcmf_cfg80211_stop_ap(ndev);
  wlan_fullmac_stop_confirm_t result = {.result_code = result_code};

  BRCMF_IFDBG(WLANIF, ndev, "Sending AP stop confirm to SME. result_code: %s",
              result_code == WLAN_STOP_RESULT_SUCCESS               ? "success"
              : result_code == WLAN_STOP_RESULT_BSS_ALREADY_STOPPED ? "already stopped"
              : result_code == WLAN_STOP_RESULT_INTERNAL_ERROR      ? "internal error"
                                                                    : "unknown");

  wlan_fullmac_impl_ifc_stop_conf(&ndev->if_proto, &result);
}

void brcmf_if_set_keys_req(net_device* ndev, const wlan_fullmac_set_keys_req_t* req,
                           wlan_fullmac_set_keys_resp_t* resp) {
  BRCMF_IFDBG(WLANIF, ndev, "Set keys request from SME. num_keys: %zu", req->num_keys);
  zx_status_t result;

  resp->num_keys = req->num_keys;
  for (size_t i = 0; i < req->num_keys; i++) {
    result = brcmf_cfg80211_add_key(ndev, &req->keylist[i]);
    if (result != ZX_OK) {
      BRCMF_WARN("Error setting key %zu: %s.", i, zx_status_get_string(result));
    }
    resp->statuslist[i] = result;
  }
}

void brcmf_if_del_keys_req(net_device* ndev, const wlan_fullmac_del_keys_req_t* req) {
  BRCMF_IFDBG(WLANIF, ndev, "Del keys request from SME. num_keys: %zu", req->num_keys);

  BRCMF_ERR("Unimplemented");
}

static void brcmf_send_eapol_confirm(net_device* ndev, const wlan_fullmac_eapol_req_t* req,
                                     zx_status_t result) {
  wlan_fullmac_eapol_confirm_t confirm;
  confirm.result_code =
      result == ZX_OK ? WLAN_EAPOL_RESULT_SUCCESS : WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE;
  std::memcpy(confirm.dst_addr, req->dst_addr, ETH_ALEN);
  BRCMF_IFDBG(WLANIF, ndev, "Sending EAPOL xmit confirm to SME. result: %s",
              confirm.result_code == WLAN_EAPOL_RESULT_SUCCESS                ? "success"
              : confirm.result_code == WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE ? "failure"
                                                                              : "unknown");
  wlan_fullmac_impl_ifc_eapol_conf(&ndev->if_proto, &confirm);
}

static void brcmf_populate_eapol_eth_header(uint8_t* dest, const wlan_fullmac_eapol_req_t* req) {
  // IEEE Std. 802.3-2015, 3.1.1
  memcpy(dest, req->dst_addr, ETH_ALEN);
  memcpy(dest + ETH_ALEN, req->src_addr, ETH_ALEN);
  *reinterpret_cast<uint16_t*>(dest + 2 * ETH_ALEN) = EAPOL_ETHERNET_TYPE_UINT16;
  memcpy(dest + 2 * ETH_ALEN + sizeof(uint16_t), req->data_list, req->data_count);
}

static void brcmf_if_eapol_req_ethernet(net_device* ndev, const wlan_fullmac_eapol_req_t* req,
                                        int length) {
  auto packet_data = std::make_unique<char[]>(length);

  brcmf_populate_eapol_eth_header(reinterpret_cast<uint8_t*>(packet_data.get()), req);

  auto packet = std::make_unique<wlan::brcmfmac::AllocatedNetbuf>(std::move(packet_data), length);
  brcmf_netdev_start_xmit(ndev, std::move(packet));

  brcmf_send_eapol_confirm(ndev, req, ZX_OK);
}

static void brcmf_if_eapol_req_netdev(net_device* ndev, const wlan_fullmac_eapol_req_t* req,
                                      int length) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_pub* drvr = ifp->drvr;
  wlan::drivers::components::FrameContainer frames = brcmf_bus_acquire_tx_space(drvr->bus_if, 1);
  if (frames.empty()) {
    BRCMF_ERR("Failed to allocate space for EAPOL transmittion");
    return;
  }

  wlan::drivers::components::Frame& frame = *frames.begin();
  frame.ShrinkHead(drvr->hdrlen);
  frame.SetPortId(ifp->ifidx);
  frame.SetPriority(0);
  frame.SetSize(length);

  brcmf_populate_eapol_eth_header(frame.Data(), req);

  cpp20::span<wlan::drivers::components::Frame> frame_span(frames);
  zx_status_t result = brcmf_start_xmit(drvr, frame_span);

  brcmf_send_eapol_confirm(ndev, req, result);
}

void brcmf_if_eapol_req(net_device* ndev, const wlan_fullmac_eapol_req_t* req) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping EAPOL xmit callback");
    return;
  }

  BRCMF_IFDBG(WLANIF, ndev, "EAPOL xmit request from SME. data_len: %zu", req->data_count);

  int packet_length;

  // Ethernet header length + EAPOL PDU length
  packet_length = 2 * ETH_ALEN + sizeof(uint16_t) + req->data_count;

  if (ndev_to_if(ndev)->drvr->device->IsNetworkDeviceBus()) {
    brcmf_if_eapol_req_netdev(ndev, req, packet_length);
  } else {
    brcmf_if_eapol_req_ethernet(ndev, req, packet_length);
  }
}

static void brcmf_get_bwcap(struct brcmf_if* ifp, uint32_t bw_cap[]) {
  // 2.4 GHz
  uint32_t val = WLC_BAND_2G;
  zx_status_t status = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);
  if (status == ZX_OK) {
    bw_cap[WLAN_BAND_TWO_GHZ] = val;

    // 5 GHz
    val = WLC_BAND_5G;
    status = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);
    if (status == ZX_OK) {
      bw_cap[WLAN_BAND_FIVE_GHZ] = val;
      return;
    }
    BRCMF_WARN(
        "Failed to retrieve 5GHz bandwidth info, but sucessfully retrieved bandwidth "
        "info for 2.4GHz bands.");
    return;
  }

  // bw_cap not supported in this version of fw
  uint32_t mimo_bwcap = 0;
  status = brcmf_fil_iovar_int_get(ifp, "mimo_bw_cap", &mimo_bwcap, nullptr);
  if (status != ZX_OK) {
    /* assume 20MHz if firmware does not give a clue */
    BRCMF_WARN("Failed to retrieve bandwidth capability info. Assuming 20MHz for all.");
    mimo_bwcap = WLC_N_BW_20ALL;
  }

  switch (mimo_bwcap) {
    case WLC_N_BW_40ALL:
      bw_cap[WLAN_BAND_TWO_GHZ] |= WLC_BW_40MHZ_BIT;
      __FALLTHROUGH;
    case WLC_N_BW_20IN2G_40IN5G:
      bw_cap[WLAN_BAND_FIVE_GHZ] |= WLC_BW_40MHZ_BIT;
      __FALLTHROUGH;
    case WLC_N_BW_20ALL:
      bw_cap[WLAN_BAND_TWO_GHZ] |= WLC_BW_20MHZ_BIT;
      bw_cap[WLAN_BAND_FIVE_GHZ] |= WLC_BW_20MHZ_BIT;
      break;
    default:
      BRCMF_ERR("invalid mimo_bw_cap value");
  }
}

static uint16_t brcmf_get_mcs_map(uint32_t nchain, uint16_t supp) {
  uint16_t mcs_map = 0xffff;
  for (uint32_t i = 0; i < nchain; i++) {
    mcs_map = (mcs_map << 2) | supp;
  }

  return mcs_map;
}

static void brcmf_update_ht_cap(struct brcmf_if* ifp, wlan_fullmac_band_capability* band_cap,
                                uint32_t bw_cap[2], uint32_t ldpc_cap, uint32_t nchain,
                                uint32_t max_ampdu_len_exp) {
  zx_status_t status;

  band_cap->ht_supported = true;

  wlan::HtCapabilities* ht_caps = wlan::HtCapabilities::View(&band_cap->ht_caps);

  // LDPC Support
  if (ldpc_cap) {
    ht_caps->ht_cap_info.set_ldpc_coding_cap(true);
  }

  // Bandwidth-related flags
  if (bw_cap[band_cap->band] & WLC_BW_40MHZ_BIT) {
    ht_caps->ht_cap_info.set_chan_width_set(true);
    ht_caps->ht_cap_info.set_short_gi_40(true);
  }
  ht_caps->ht_cap_info.set_short_gi_20(true);
  ht_caps->ht_cap_info.set_dsss_in_40(true);

  // SM Power Save
  // At present SMPS appears to never be enabled in firmware (see fxbug.dev/29648)
  ht_caps->ht_cap_info.set_sm_power_save(IEEE80211_HT_CAPS_SMPS_DISABLED);

  // Rx STBC
  uint32_t rx_stbc = 0;
  (void)brcmf_fil_iovar_int_get(ifp, "stbc_rx", &rx_stbc, nullptr);
  ht_caps->ht_cap_info.set_rx_stbc(rx_stbc & 0x3);

  // Tx STBC
  // According to Broadcom, Tx STBC capability should be induced from the value of the
  // "stbc_rx" iovar and not "stbc_tx".
  if (rx_stbc != 0) {
    ht_caps->ht_cap_info.set_tx_stbc(true);
  }

  // AMPDU Parameters
  uint32_t ampdu_rx_density = 0;
  status = brcmf_fil_iovar_int_get(ifp, "ampdu_rx_density", &ampdu_rx_density, nullptr);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to retrieve value for AMPDU Rx density from firmware, using 16 us");
    ampdu_rx_density = 7;
  }
  ht_caps->ampdu_params.set_min_start_spacing(ampdu_rx_density & 0x7);
  if (max_ampdu_len_exp > 3) {
    // Cap A-MPDU length at 64K
    max_ampdu_len_exp = 3;
  }
  ht_caps->ampdu_params.set_exponent(max_ampdu_len_exp);

  // Supported MCS Set
  size_t mcs_set_size = sizeof(ht_caps->mcs_set);
  if (nchain > mcs_set_size) {
    BRCMF_ERR("Supported MCS set too small for nchain (%u), truncating", nchain);
    nchain = mcs_set_size;
  }
  memset(&ht_caps->mcs_set, 0xff, nchain);
}

static void brcmf_update_vht_cap(struct brcmf_if* ifp, wlan_fullmac_band_capability* band_cap,
                                 uint32_t bw_cap[2], uint32_t nchain, uint32_t ldpc_cap,
                                 uint32_t max_ampdu_len_exp) {
  uint16_t mcs_map;

  band_cap->vht_supported = true;

  wlan::VhtCapabilities* vht_caps = wlan::VhtCapabilities::View(&band_cap->vht_caps);

  // Set Max MPDU length to 11454
  // TODO (fxbug.dev/29107): Value hardcoded from firmware behavior of the BCM4356 and BCM4359
  // chips.
  vht_caps->vht_cap_info.set_max_mpdu_len(2);

  /* 80MHz is mandatory */
  vht_caps->vht_cap_info.set_sgi_cbw80(true);
  if (bw_cap[band_cap->band] & WLC_BW_160MHZ_BIT) {
    vht_caps->vht_cap_info.set_supported_cbw_set(1);
    vht_caps->vht_cap_info.set_sgi_cbw160(true);
  }

  if (ldpc_cap) {
    vht_caps->vht_cap_info.set_rx_ldpc(true);
  }

  // Tx STBC
  // TODO (fxbug.dev/29107): Value is hardcoded for now
  if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
    vht_caps->vht_cap_info.set_tx_stbc(true);
  }

  /* all support 256-QAM */
  mcs_map = brcmf_get_mcs_map(nchain, IEEE80211_VHT_MCS_0_9);
  /* Rx MCS map (B0:15) + Tx MCS map (B0:15) */
  vht_caps->vht_mcs_nss.set_as_uint64((uint64_t)mcs_map | ((uint64_t)mcs_map << 32));

  /* Beamforming support information */
  uint32_t txbf_bfe_cap = 0;
  uint32_t txbf_bfr_cap = 0;

  // Use the *_cap_hw value when possible, since the reflects the capabilities of the device
  // regardless of current operating mode.
  zx_status_t status;
  status = brcmf_fil_iovar_int_get(ifp, "txbf_bfe_cap_hw", &txbf_bfe_cap, nullptr);
  if (status != ZX_OK) {
    (void)brcmf_fil_iovar_int_get(ifp, "txbf_bfe_cap", &txbf_bfe_cap, nullptr);
  }
  status = brcmf_fil_iovar_int_get(ifp, "txbf_bfr_cap_hw", &txbf_bfr_cap, nullptr);
  if (status != ZX_OK) {
    BRCMF_DBG(FIL, "Failed to get iovar txbf_bfr_cap_hw. Falling back to txbf_bfr_cap.");
    (void)brcmf_fil_iovar_int_get(ifp, "txbf_bfr_cap", &txbf_bfr_cap, nullptr);
  }

  if (txbf_bfe_cap & BRCMF_TXBF_SU_BFE_CAP) {
    vht_caps->vht_cap_info.set_su_bfee(true);
  }
  if (txbf_bfe_cap & BRCMF_TXBF_MU_BFE_CAP) {
    vht_caps->vht_cap_info.set_mu_bfee(true);
  }
  if (txbf_bfr_cap & BRCMF_TXBF_SU_BFR_CAP) {
    vht_caps->vht_cap_info.set_su_bfer(true);
  }
  if (txbf_bfr_cap & BRCMF_TXBF_MU_BFR_CAP) {
    vht_caps->vht_cap_info.set_mu_bfer(true);
  }

  uint32_t txstreams = 0;
  // txstreams_cap is not supported in all firmware versions, but when it is supported it
  // provides capability info regardless of current operating state.
  status = brcmf_fil_iovar_int_get(ifp, "txstreams_cap", &txstreams, nullptr);
  if (status != ZX_OK) {
    (void)brcmf_fil_iovar_int_get(ifp, "txstreams", &txstreams, nullptr);
  }

  if ((txbf_bfe_cap || txbf_bfr_cap) && (txstreams > 1)) {
    vht_caps->vht_cap_info.set_bfee_sts(2);
    vht_caps->vht_cap_info.set_num_sounding(txstreams - 1);
    // Link adapt = Both
    vht_caps->vht_cap_info.set_link_adapt(3);
  }

  // Maximum A-MPDU Length Exponent
  vht_caps->vht_cap_info.set_max_ampdu_exp(max_ampdu_len_exp);
}

static void brcmf_dump_80211_ht_caps(ht_capabilities_t* caps) {
  // wlan::HtCapabilities
  wlan::HtCapabilities* ht_caps = wlan::HtCapabilities::View(caps);
  BRCMF_DBG_UNFILTERED("     ht_cap_info: %#x", ht_caps->ht_cap_info.as_uint16());
  BRCMF_DBG_UNFILTERED("     ampdu_params: %#x", ht_caps->ampdu_params.val());

  char mcs_set_str[std::size(ht_caps->mcs_set.val()) * 5 + 1];
  char* str = mcs_set_str;
  for (unsigned i = 0; i < std::size(ht_caps->mcs_set.val()); i++) {
    str += sprintf(str, "%s0x%02hhx", i > 0 ? " " : "", ht_caps->mcs_set.val()[i]);
  }

  BRCMF_DBG_UNFILTERED("     mcs_set: %s", mcs_set_str);
  BRCMF_DBG_UNFILTERED("     ht_ext_cap: %#x", ht_caps->ht_ext_cap.as_uint16());
  BRCMF_DBG_UNFILTERED("     asel_cap: %#x", ht_caps->asel_cap.val());
}

static void brcmf_dump_80211_vht_caps(vht_capabilities_t* caps) {
  wlan::VhtCapabilities* vht_caps = wlan::VhtCapabilities::View(caps);
  BRCMF_DBG_UNFILTERED("     vht_cap_info: %#lx", vht_caps->vht_cap_info.as_uint32());
  BRCMF_DBG_UNFILTERED("     vht_mcs_nss: %#" PRIx64 "", vht_caps->vht_mcs_nss.as_uint64());
}

static void brcmf_dump_if_band_cap(wlan_fullmac_band_capability_t* band_cap) {
  char band_str[32];
  switch (band_cap->band) {
    case WLAN_BAND_TWO_GHZ:
      sprintf(band_str, "2GHz");
      break;
    case WLAN_BAND_FIVE_GHZ:
      sprintf(band_str, "5GHz");
      break;
    default:
      sprintf(band_str, "unknown (%d)", band_cap->band);
      break;
  }
  BRCMF_DBG_UNFILTERED("   band: %s", band_str);

  if (band_cap->basic_rate_count > fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES) {
    BRCMF_DBG_UNFILTERED("Number of rates reported (%u) exceeds limit (%du), truncating",
                         band_cap->basic_rate_count,
                         fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES);
    band_cap->basic_rate_count = fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES;
  }
  char rates_str[fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES * 6 + 1];
  char* str = rates_str;
  for (unsigned i = 0; i < band_cap->basic_rate_count; i++) {
    str += sprintf(str, "%s%d", i > 0 ? " " : "", band_cap->basic_rate_list[i]);
  }
  BRCMF_DBG_UNFILTERED("     basic_rates: %s", rates_str);

  if (band_cap->operating_channel_count > fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS) {
    BRCMF_DBG_UNFILTERED("Number of channels reported (%u) exceeds limit (%du), truncating",
                         band_cap->operating_channel_count,
                         fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS);
    band_cap->operating_channel_count = fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS;
  }
  char channels_str[fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS * 4 + 1];
  str = channels_str;
  for (unsigned i = 0; i < band_cap->operating_channel_count; i++) {
    str += sprintf(str, "%s%d", i > 0 ? " " : "", band_cap->operating_channel_list[i]);
  }
  BRCMF_DBG_UNFILTERED("     channels: %s", channels_str);

  BRCMF_DBG_UNFILTERED("     ht_supported: %s", band_cap->ht_supported ? "true" : "false");
  if (band_cap->ht_supported) {
    brcmf_dump_80211_ht_caps(&band_cap->ht_caps);
  }

  BRCMF_DBG_UNFILTERED("     vht_supported: %s", band_cap->vht_supported ? "true" : "false");
  if (band_cap->vht_supported) {
    brcmf_dump_80211_vht_caps(&band_cap->vht_caps);
  }
}

static void brcmf_dump_if_query_info(wlan_fullmac_query_info_t* info) {
  BRCMF_DBG_UNFILTERED(" Device capabilities as reported to wlanif:");
  BRCMF_DBG_UNFILTERED("   sta_addr: " FMT_MAC, FMT_MAC_ARGS(info->sta_addr));
  BRCMF_DBG_UNFILTERED("   role(s): %s%s%s", info->role & WLAN_MAC_ROLE_CLIENT ? "client " : "",
                       info->role & WLAN_MAC_ROLE_AP ? "ap " : "",
                       info->role & WLAN_MAC_ROLE_MESH ? "mesh " : "");
  BRCMF_DBG_UNFILTERED("   feature(s): %s%s",
                       info->features & WLAN_FULLMAC_FEATURE_DMA ? "DMA " : "",
                       info->features & WLAN_FULLMAC_FEATURE_SYNTH ? "SYNTH " : "");
  for (unsigned i = 0; i < info->band_cap_count; i++) {
    brcmf_dump_if_band_cap(&info->band_cap_list[i]);
  }
}

void brcmf_if_query(net_device* ndev, wlan_fullmac_query_info_t* info) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct wireless_dev* wdev = ndev_to_wdev(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  struct brcmf_chanspec_list* list = nullptr;
  uint32_t nmode = 0;
  uint32_t vhtmode = 0;
  uint32_t rxchain = 0, nchain = 0;
  uint32_t bw_cap[2] = {WLC_BW_20MHZ_BIT, WLC_BW_20MHZ_BIT};
  uint32_t ldpc_cap = 0;
  uint32_t max_ampdu_len_exp = 0;
  zx_status_t status;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_IFDBG(WLANIF, ndev, "Query request received from SME.");

  memset(info, 0, sizeof(*info));

  // mac_addr
  memcpy(info->sta_addr, ifp->mac_addr, ETH_ALEN);

  // role
  info->role = wdev->iftype;

  // bands
  uint32_t bandlist[3];
  status = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BANDLIST, &bandlist, sizeof(bandlist), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("could not obtain band info: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    return;
  }

  wlan_fullmac_band_capability_t* band_cap_2ghz = nullptr;
  wlan_fullmac_band_capability_t* band_cap_5ghz = nullptr;

  /* first entry in bandlist is number of bands */
  info->band_cap_count = bandlist[0];
  for (unsigned i = 1; i <= info->band_cap_count && i < std::size(bandlist); i++) {
    if (i > std::size(info->band_cap_list)) {
      BRCMF_ERR("insufficient space in query response for all bands, truncating");
      continue;
    }
    wlan_fullmac_band_capability* band_cap = &info->band_cap_list[i - 1];
    if (bandlist[i] == WLC_BAND_2G) {
      band_cap->band = WLAN_BAND_TWO_GHZ;
      band_cap->basic_rate_count =
          std::min<size_t>(fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES, wl_g_rates_size);
      memcpy(band_cap->basic_rate_list, wl_g_rates,
             band_cap->basic_rate_count * sizeof(*band_cap->basic_rate_list));
      band_cap_2ghz = band_cap;
    } else if (bandlist[i] == WLC_BAND_5G) {
      band_cap->band = WLAN_BAND_FIVE_GHZ;
      band_cap->basic_rate_count =
          std::min<size_t>(fuchsia_wlan_internal_MAX_SUPPORTED_BASIC_RATES, wl_a_rates_size);
      memcpy(band_cap->basic_rate_list, wl_a_rates,
             band_cap->basic_rate_count * sizeof(*band_cap->basic_rate_list));
      band_cap_5ghz = band_cap;
    }
  }

  // channels
  uint8_t* pbuf = static_cast<decltype(pbuf)>(calloc(BRCMF_DCMD_MEDLEN, 1));
  if (pbuf == nullptr) {
    BRCMF_ERR("unable to allocate memory for channel information");
    return;
  }

  status = brcmf_fil_iovar_data_get(ifp, "chanspecs", pbuf, BRCMF_DCMD_MEDLEN, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("get chanspecs error: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    goto fail_pbuf;
  }
  list = (struct brcmf_chanspec_list*)pbuf;
  for (uint32_t i = 0; i < list->count; i++) {
    struct brcmu_chan ch;
    ch.chspec = list->element[i];
    cfg->d11inf.decchspec(&ch);

    // Find the appropriate band
    wlan_fullmac_band_capability_t* band_cap = nullptr;
    if (ch.band == BRCMU_CHAN_BAND_2G) {
      band_cap = band_cap_2ghz;
    } else if (ch.band == BRCMU_CHAN_BAND_5G) {
      band_cap = band_cap_5ghz;
    } else {
      BRCMF_ERR("unrecognized band for channel %d", ch.control_ch_num);
      continue;
    }
    if (band_cap == nullptr) {
      continue;
    }

    // Fuchsia's wlan channels are simply the control channel (for now), whereas
    // brcm specifies each channel + bw + sb configuration individually. Until we
    // offer that level of resolution, just filter out duplicates.
    uint32_t j;
    for (j = 0; j < band_cap->operating_channel_count; j++) {
      if (band_cap->operating_channel_list[j] == ch.control_ch_num) {
        break;
      }
    }
    if (j != band_cap->operating_channel_count) {
      continue;
    }
    if (band_cap->operating_channel_count + 1 >
        sizeof(band_cap->operating_channel_list) / sizeof(band_cap->operating_channel_list[0])) {
      BRCMF_ERR("Insufficient space for channel %d", ch.control_ch_num);
      BRCMF_ERR("Skipping all remaining channels: %d skipped", list->count - i);
      break;
    }

    band_cap->operating_channel_list[band_cap->operating_channel_count++] = ch.control_ch_num;
  }

  // Parse HT/VHT information
  nmode = 0;
  vhtmode = 0;
  rxchain = 0;
  nchain = 0;
  (void)brcmf_fil_iovar_int_get(ifp, "vhtmode", &vhtmode, nullptr);
  status = brcmf_fil_iovar_int_get(ifp, "nmode", &nmode, &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("nmode error: %s, fw err %s. Assuming both HT mode and VHT mode are not available.",
              zx_status_get_string(status), brcmf_fil_get_errstr(fw_err));
    // VHT requires HT support
    vhtmode = 0;
  } else {
    brcmf_get_bwcap(ifp, bw_cap);
  }
  BRCMF_DBG(QUERY, "nmode=%d, vhtmode=%d, bw_cap=(%d, %d)", nmode, vhtmode,
            bw_cap[WLAN_BAND_TWO_GHZ], bw_cap[WLAN_BAND_FIVE_GHZ]);

  // LDPC support, applies to both HT and VHT
  ldpc_cap = 0;
  (void)brcmf_fil_iovar_int_get(ifp, "ldpc_cap", &ldpc_cap, nullptr);

  // Max AMPDU length
  max_ampdu_len_exp = 0;
  status = brcmf_fil_iovar_int_get(ifp, "ampdu_rx_factor", &max_ampdu_len_exp, nullptr);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to retrieve value for AMPDU maximum Rx length. Using 8191 bytes");
  }

  // Rx chains (and streams)
  // The "rxstreams_cap" iovar, when present, indicates the maximum number of Rx streams
  // possible, encoded as one bit per stream (i.e., a value of 0x3 indicates 2 streams/chains).
  if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
    // TODO (fxbug.dev/29107): The BCM4359 firmware supports rxstreams_cap, but it returns 0x2
    // instead of 0x3, which is incorrect.
    rxchain = 0x3;
  } else {
    // According to Broadcom, rxstreams_cap, when available, is an accurate representation of
    // the number of rx chains.
    status = brcmf_fil_iovar_int_get(ifp, "rxstreams_cap", &rxchain, nullptr);
    if (status != ZX_OK) {
      // TODO (fxbug.dev/29107): The rxstreams_cap iovar isn't yet supported in the BCM4356
      // firmware. For now we use a hard-coded value (another option would be to parse the
      // nvram contents ourselves (looking for the value associated with the key "rxchain").
      BRCMF_INFO("Failed to retrieve value for Rx chains. Assuming chip supports 2 Rx chains.");
      rxchain = 0x3;
    }
  }

  for (nchain = 0; rxchain; nchain++) {
    rxchain = rxchain & (rxchain - 1);
  }
  BRCMF_DBG(QUERY, "nchain=%d", nchain);

  if (nmode) {
    if (band_cap_2ghz) {
      brcmf_update_ht_cap(ifp, band_cap_2ghz, bw_cap, ldpc_cap, nchain, max_ampdu_len_exp);
    }
    if (band_cap_5ghz) {
      brcmf_update_ht_cap(ifp, band_cap_5ghz, bw_cap, ldpc_cap, nchain, max_ampdu_len_exp);
    }
  }
  if (vhtmode && band_cap_5ghz) {
    brcmf_update_vht_cap(ifp, band_cap_5ghz, bw_cap, nchain, ldpc_cap, max_ampdu_len_exp);
  }

  if (BRCMF_IS_ON(QUERY)) {
    brcmf_dump_if_query_info(info);
  }

fail_pbuf:
  free(pbuf);
}

void brcmf_if_query_mac_sublayer_support(net_device* ndev, mac_sublayer_support_t* resp) {
  BRCMF_IFDBG(WLANIF, ndev, "Query MAC sublayer feature support request received from SME.");

  memset(resp, 0, sizeof(*resp));
  if (ndev_to_if(ndev)->drvr->device->IsNetworkDeviceBus()) {
    resp->data_plane.data_plane_type = DATA_PLANE_TYPE_GENERIC_NETWORK_DEVICE;
  } else {
    resp->data_plane.data_plane_type = DATA_PLANE_TYPE_ETHERNET_DEVICE;
  }
  resp->device.mac_implementation_type = MAC_IMPLEMENTATION_TYPE_FULLMAC;
}

void brcmf_if_query_security_support(net_device* ndev, security_support_t* resp) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  BRCMF_IFDBG(WLANIF, ndev, "Query security feature support request received from SME.");

  memset(resp, 0, sizeof(*resp));

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_EXTSAE)) {
    resp->sae.sme_handler_supported = true;
  }

  resp->mfp.supported = brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFP);
}

void brcmf_if_query_spectrum_management_support(net_device* ndev,
                                                spectrum_management_support_t* resp) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  BRCMF_IFDBG(WLANIF, ndev, "Query spectrum management support request received from SME.");

  memset(resp, 0, sizeof(*resp));

  resp->dfs.supported = brcmf_feat_is_enabled(ifp, BRCMF_FEAT_DFS);
}

namespace {

zx_status_t brcmf_convert_antenna_id(const histograms_report_t& histograms_report,
                                     wlan_fullmac_antenna_id_t* out_antenna_id) {
  switch (histograms_report.antennaid.freq) {
    case ANTENNA_2G:
      out_antenna_id->freq = WLAN_FULLMAC_ANTENNA_FREQ_ANTENNA_2_G;
      break;
    case ANTENNA_5G:
      out_antenna_id->freq = WLAN_FULLMAC_ANTENNA_FREQ_ANTENNA_5_G;
      break;
    default:
      return ZX_ERR_OUT_OF_RANGE;
  }
  out_antenna_id->index = histograms_report.antennaid.idx;
  return ZX_OK;
}

void brcmf_get_noise_floor_samples(const histograms_report_t& histograms_report,
                                   std::vector<wlan_fullmac_hist_bucket_t>* out_noise_floor_samples,
                                   uint64_t* out_invalid_samples) {
  for (size_t i = 0; i < WLAN_FULLMAC_MAX_NOISE_FLOOR_SAMPLES; ++i) {
    wlan_fullmac_hist_bucket_t bucket;
    bucket.bucket_index = i;
    bucket.num_samples = histograms_report.rxnoiseflr[i];
    out_noise_floor_samples->push_back(bucket);
  }
  // rxnoiseflr has an extra bucket. If there is anything in it, it is invalid.
  *out_invalid_samples = histograms_report.rxsnr[255];
}

void brcmf_get_rssi_samples(const histograms_report_t& histograms_report,
                            std::vector<wlan_fullmac_hist_bucket_t>* out_rssi_samples,
                            uint64_t* out_invalid_samples) {
  for (size_t i = 0; i < WLAN_FULLMAC_MAX_RSSI_SAMPLES; ++i) {
    wlan_fullmac_hist_bucket_t bucket;
    bucket.bucket_index = i;
    bucket.num_samples = histograms_report.rxrssi[i];
    out_rssi_samples->push_back(bucket);
  }
  // rxrssi has an extra bucket. If there is anything in it, it is invalid.
  *out_invalid_samples = histograms_report.rxrssi[255];
}

void brcmf_get_snr_samples(const histograms_report_t& histograms_report,
                           std::vector<wlan_fullmac_hist_bucket_t>* out_snr_samples,
                           uint64_t* out_invalid_samples) {
  for (size_t i = 0; i < WLAN_FULLMAC_MAX_SNR_SAMPLES; ++i) {
    wlan_fullmac_hist_bucket_t bucket;
    bucket.bucket_index = i;
    bucket.num_samples = histograms_report.rxsnr[i];
    out_snr_samples->push_back(bucket);
  }
  // rxsnr does not have any indices that should be considered invalid buckets.
  *out_invalid_samples = 0;
}

void brcmf_get_rx_rate_index_samples(
    const histograms_report_t& histograms_report,
    std::vector<wlan_fullmac_hist_bucket_t>* out_rx_rate_index_samples,
    uint64_t* out_invalid_samples) {
  uint32_t rxrate[WLAN_FULLMAC_MAX_RX_RATE_INDEX_SAMPLES];
  brcmu_set_rx_rate_index_hist_rx11ac(histograms_report.rx11ac, rxrate);
  brcmu_set_rx_rate_index_hist_rx11b(histograms_report.rx11b, rxrate);
  brcmu_set_rx_rate_index_hist_rx11g(histograms_report.rx11g, rxrate);
  brcmu_set_rx_rate_index_hist_rx11n(histograms_report.rx11n, rxrate);
  for (uint8_t i = 0; i < WLAN_FULLMAC_MAX_RX_RATE_INDEX_SAMPLES; ++i) {
    wlan_fullmac_hist_bucket_t bucket;
    bucket.bucket_index = i;
    bucket.num_samples = rxrate[i];
    out_rx_rate_index_samples->push_back(bucket);
  }
  // rxrate does not have any indices that should be considered invalid buckets.
  *out_invalid_samples = 0;
}

void brcmf_convert_histograms_report_noise_floor(
    const histograms_report_t& histograms_report, const wlan_fullmac_antenna_id_t& antenna_id,
    wlan_fullmac_noise_floor_histogram_t* out_hist,
    std::vector<wlan_fullmac_hist_bucket_t>* out_samples) {
  out_hist->antenna_id = antenna_id;
  out_hist->hist_scope = WLAN_FULLMAC_HIST_SCOPE_PER_ANTENNA;
  brcmf_get_noise_floor_samples(histograms_report, out_samples, &out_hist->invalid_samples);
  out_hist->noise_floor_samples_count = out_samples->size();
  out_hist->noise_floor_samples_list = out_samples->data();
}

void brcmf_convert_histograms_report_rx_rate_index(
    const histograms_report_t& histograms_report, const wlan_fullmac_antenna_id_t& antenna_id,
    wlan_fullmac_rx_rate_index_histogram_t* out_hist,
    std::vector<wlan_fullmac_hist_bucket_t>* out_samples) {
  out_hist->antenna_id = antenna_id;
  out_hist->hist_scope = WLAN_FULLMAC_HIST_SCOPE_PER_ANTENNA;
  brcmf_get_rx_rate_index_samples(histograms_report, out_samples, &out_hist->invalid_samples);
  out_hist->rx_rate_index_samples_count = out_samples->size();
  out_hist->rx_rate_index_samples_list = out_samples->data();
}

void brcmf_convert_histograms_report_rssi(const histograms_report_t& histograms_report,
                                          const wlan_fullmac_antenna_id_t& antenna_id,
                                          wlan_fullmac_rssi_histogram_t* out_hist,
                                          std::vector<wlan_fullmac_hist_bucket_t>* out_samples) {
  out_hist->antenna_id = antenna_id;
  out_hist->hist_scope = WLAN_FULLMAC_HIST_SCOPE_PER_ANTENNA;
  brcmf_get_rssi_samples(histograms_report, out_samples, &out_hist->invalid_samples);
  out_hist->rssi_samples_count = out_samples->size();
  out_hist->rssi_samples_list = out_samples->data();
}

void brcmf_convert_histograms_report_snr(const histograms_report_t& histograms_report,
                                         const wlan_fullmac_antenna_id_t& antenna_id,
                                         wlan_fullmac_snr_histogram_t* out_hist,
                                         std::vector<wlan_fullmac_hist_bucket_t>* out_samples) {
  out_hist->antenna_id = antenna_id;
  out_hist->hist_scope = WLAN_FULLMAC_HIST_SCOPE_PER_ANTENNA;
  brcmf_get_snr_samples(histograms_report, out_samples, &out_hist->invalid_samples);
  out_hist->snr_samples_count = out_samples->size();
  out_hist->snr_samples_list = out_samples->data();
}

zx_status_t brcmf_get_histograms_report(brcmf_if* ifp, histograms_report_t* out_report) {
  if (ifp == nullptr) {
    BRCMF_ERR("Invalid interface");
    return ZX_ERR_INTERNAL;
  }
  if (out_report == nullptr) {
    BRCMF_ERR("Invalid histograms_report_t pointer");
    return ZX_ERR_INTERNAL;
  }

  bcme_status_t fw_err = BCME_OK;
  wl_wstats_cnt_t wl_stats_cnt;
  std::memset(&wl_stats_cnt, 0, sizeof(wl_wstats_cnt_t));
  const auto wstats_counters_status = brcmf_fil_iovar_data_get(
      ifp, "wstats_counters", &wl_stats_cnt, sizeof(wl_wstats_cnt_t), &fw_err);
  if (wstats_counters_status != ZX_OK) {
    BRCMF_ERR("Failed to get wstats_counters: %s, fw err %s",
              zx_status_get_string(wstats_counters_status), brcmf_fil_get_errstr(fw_err));
    return wstats_counters_status;
  }

  uint32_t chanspec = 0;
  const auto chanspec_status = brcmf_fil_iovar_int_get(ifp, "chanspec", &chanspec, &fw_err);
  if (chanspec_status != ZX_OK) {
    BRCMF_ERR("Failed to retrieve chanspec: %s, fw err %s", zx_status_get_string(chanspec_status),
              brcmf_fil_get_errstr(fw_err));
    return chanspec_status;
  }

  uint32_t version;
  const auto version_status = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_VERSION, &version, &fw_err);
  if (version_status != ZX_OK) {
    BRCMF_ERR("Failed to retrieve version: %s, fw err %s", zx_status_get_string(version_status),
              brcmf_fil_get_errstr(fw_err));
    return version_status;
  }

  uint32_t rxchain = 0;
  const auto rxchain_status = brcmf_fil_iovar_int_get(ifp, "rxchain", &rxchain, &fw_err);
  if (rxchain_status != ZX_OK) {
    BRCMF_ERR("Failed to retrieve rxchain: %s, fw err %s", zx_status_get_string(rxchain_status),
              brcmf_fil_get_errstr(fw_err));
    return rxchain_status;
  }

  const bool get_histograms_success =
      get_histograms(wl_stats_cnt, static_cast<chanspec_t>(chanspec), version, rxchain, out_report);
  if (get_histograms_success) {
    return ZX_OK;
  }
  BRCMF_ERR("Failed to get per-antenna metrics");
  return ZX_ERR_INTERNAL;
}

}  // namespace

zx_status_t brcmf_if_get_iface_counter_stats(net_device* ndev,
                                             wlan_fullmac_iface_counter_stats_t* out_stats) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping get iface counter stats");
    return ZX_ERR_INTERNAL;
  }

  struct brcmf_if* ifp = ndev_to_if(ndev);

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    // MFG builds do not support many of the stats iovars.
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
    return ZX_ERR_NOT_CONNECTED;
  }

  brcmf_pktcnt_le pktcnt;
  bcme_status_t fw_err;
  zx_status_t status =
      brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_GET_PKTCNTS, &pktcnt, sizeof(pktcnt), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("could not get pkt cnts: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    return status;
  }

  BRCMF_DBG(DATA, "Cntrs: rxgood:%d rxbad:%d txgood:%d txbad:%d rxocast:%d", pktcnt.rx_good_pkt,
            pktcnt.rx_bad_pkt, pktcnt.tx_good_pkt, pktcnt.tx_bad_pkt, pktcnt.rx_ocast_good_pkt);

  out_stats->rx_unicast_total = pktcnt.rx_good_pkt + pktcnt.rx_bad_pkt + ndev->stats.rx_errors;
  out_stats->rx_unicast_drop = pktcnt.rx_bad_pkt + ndev->stats.rx_errors;
  out_stats->rx_multicast = pktcnt.rx_ocast_good_pkt;
  out_stats->tx_total = pktcnt.tx_good_pkt + pktcnt.tx_bad_pkt + ndev->stats.tx_dropped;
  out_stats->tx_drop = pktcnt.tx_bad_pkt + ndev->stats.tx_dropped;

  return ZX_OK;
}

zx_status_t brcmf_if_get_iface_histogram_stats(net_device* ndev,
                                               wlan_fullmac_iface_histogram_stats_t* out_stats) {
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping get iface histogram stats");
    return ZX_ERR_INTERNAL;
  }
  struct brcmf_if* ifp = ndev_to_if(ndev);

  ndev->stats.noise_floor_histograms = {};
  ndev->stats.noise_floor_samples = {};
  ndev->stats.rssi_histograms = {};
  ndev->stats.rssi_samples = {};
  ndev->stats.rx_rate_index_histograms = {};
  ndev->stats.rx_rate_index_samples = {};
  ndev->stats.snr_histograms = {};
  ndev->stats.snr_samples = {};

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
    // MFG builds do not support many of the stats iovars.
    return ZX_ERR_NOT_SUPPORTED;
  }

  // If detailed histogram feature is not enabled, do nothing.
  if (!brcmf_feat_is_enabled(ifp->drvr, BRCMF_FEAT_DHIST)) {
    return ZX_ERR_NOT_CONNECTED;
  }

  if (!brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
    return ZX_ERR_NOT_CONNECTED;
  }

  histograms_report_t histograms_report;
  const auto hist_status = brcmf_get_histograms_report(ifp, &histograms_report);
  if (hist_status != ZX_OK) {
    return hist_status;
  }
  wlan_fullmac_antenna_id_t antenna_id;
  const auto antenna_id_status = brcmf_convert_antenna_id(histograms_report, &antenna_id);
  if (antenna_id_status != ZX_OK) {
    BRCMF_ERR("Invalid antenna ID, freq: %d idx: %d", histograms_report.antennaid.freq,
              histograms_report.antennaid.idx);
    return antenna_id_status;
  }
  ndev->stats.noise_floor_histograms.resize(1);
  brcmf_convert_histograms_report_noise_floor(histograms_report, antenna_id,
                                              ndev->stats.noise_floor_histograms.data(),
                                              &ndev->stats.noise_floor_samples);
  ndev->stats.rx_rate_index_histograms.resize(1);
  brcmf_convert_histograms_report_rx_rate_index(histograms_report, antenna_id,
                                                ndev->stats.rx_rate_index_histograms.data(),
                                                &ndev->stats.rx_rate_index_samples);
  ndev->stats.rssi_histograms.resize(1);
  brcmf_convert_histograms_report_rssi(
      histograms_report, antenna_id, ndev->stats.rssi_histograms.data(), &ndev->stats.rssi_samples);
  ndev->stats.snr_histograms.resize(1);
  brcmf_convert_histograms_report_snr(histograms_report, antenna_id,
                                      ndev->stats.snr_histograms.data(), &ndev->stats.snr_samples);

  out_stats->noise_floor_histograms_count = ndev->stats.noise_floor_histograms.size();
  out_stats->noise_floor_histograms_list = ndev->stats.noise_floor_histograms.data();
  out_stats->rssi_histograms_count = ndev->stats.rssi_histograms.size();
  out_stats->rssi_histograms_list = ndev->stats.rssi_histograms.data();
  out_stats->rx_rate_index_histograms_count = ndev->stats.rx_rate_index_histograms.size();
  out_stats->rx_rate_index_histograms_list = ndev->stats.rx_rate_index_histograms.data();
  out_stats->snr_histograms_count = ndev->stats.snr_histograms.size();
  out_stats->snr_histograms_list = ndev->stats.snr_histograms.data();

  return ZX_OK;
}

void brcmf_if_data_queue_tx(net_device* ndev, uint32_t options, ethernet_netbuf_t* netbuf,
                            ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  auto b = std::make_unique<wlan::brcmfmac::EthernetNetbuf>(netbuf, completion_cb, cookie);
  brcmf_netdev_start_xmit(ndev, std::move(b));
}

zx_status_t brcmf_if_sae_handshake_resp(net_device* ndev,
                                        const wlan_fullmac_sae_handshake_resp_t* resp) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  bcme_status_t fw_err = BCME_OK;
  zx_status_t err = ZX_OK;

  if (!resp) {
    BRCMF_ERR("Invalid arguments, resp is nullptr.");
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_EXTERNAL_REASON);
    return ZX_ERR_INVALID_ARGS;
  }

  if (memcmp(resp->peer_sta_address, ifp->connect_req.selected_bss.bssid, ETH_ALEN)) {
    BRCMF_ERR("Auth MAC != Join MAC");
#if !defined(NDEBUG)
    const uint8_t* old_mac = ifp->connect_req.selected_bss.bssid;
    const uint8_t* new_mac = resp->peer_sta_address;
    BRCMF_DBG(CONN, " auth mac: " FMT_MAC ", join mac: " FMT_MAC, FMT_MAC_ARGS(new_mac),
              FMT_MAC_ARGS(old_mac));
#endif /* !defined(NDEBUG) */
    // Just in case, in debug builds, we should investigate why the MLME is giving us inconsistent
    // requests.
    ZX_DEBUG_ASSERT(0);

    // In release builds, ignore and continue.
    BRCMF_ERR("Ignoring mismatch and using join MAC address");
  }

  auto ssid = brcmf_find_ssid_in_ies(ifp->connect_req.selected_bss.ies_list,
                                     ifp->connect_req.selected_bss.ies_count);
  if (ssid.empty()) {
    BRCMF_ERR("No SSID IE in BSS");
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
  }

  brcmf_clear_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state);

  // Issue assoc_mgr_cmd to resume firmware from waiting for the success of SAE authentication.
  assoc_mgr_cmd_t cmd;
  cmd.version = ASSOC_MGR_CURRENT_VERSION;
  cmd.length = sizeof(cmd);
  cmd.cmd = ASSOC_MGR_CMD_PAUSE_ON_EVT;
  cmd.params = ASSOC_MGR_PARAMS_EVENT_NONE;

  err = brcmf_fil_iovar_data_set(ifp, "assoc_mgr_cmd", &cmd, sizeof(cmd), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Set iovar assoc_mgr_cmd fail. err: %s, fw_err: %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
  }

  return err;
}

zx_status_t brcmf_if_sae_frame_tx(net_device* ndev, const wlan_fullmac_sae_frame_t* frame) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  bcme_status_t fw_err = BCME_OK;
  zx_status_t err = ZX_OK;

  // Mac header(24 bytes) + Auth frame header(6 bytes) + sae_fields length.
  uint32_t frame_size =
      sizeof(wlan::MgmtFrameHeader) + sizeof(wlan::Authentication) + frame->sae_fields_count;
  // Carry the SAE authentication frame in the last field of assoc_mgr_cmd.
  uint32_t cmd_buf_len = sizeof(assoc_mgr_cmd_t) + frame_size;
  uint8_t cmd_buf[cmd_buf_len];
  assoc_mgr_cmd_t* cmd = reinterpret_cast<assoc_mgr_cmd_t*>(cmd_buf);
  cmd->version = ASSOC_MGR_CURRENT_VERSION;
  // As the description of "length" field in this structure, it should be used to store the length
  // of the entire structure, here is a special case where we store the length of the frame here.
  // After confirming with vendor, this is the way they deal with extra data for this iovar, the
  // value of "length" field should be the length of extra data.
  cmd->length = frame_size;
  cmd->cmd = ASSOC_MGR_CMD_SEND_AUTH;

  auto sae_frame =
      reinterpret_cast<brcmf_sae_auth_frame*>(cmd_buf + offsetof(assoc_mgr_cmd_t, params));

  // Set MAC addresses in MAC header, firmware will check these parts, and fill other missing parts.
  sae_frame->mac_hdr.addr1 = wlan::common::MacAddr(frame->peer_sta_address);  // DA
  sae_frame->mac_hdr.addr2 = wlan::common::MacAddr(ifp->mac_addr);            // SA
  sae_frame->mac_hdr.addr3 = wlan::common::MacAddr(frame->peer_sta_address);  // BSSID

  BRCMF_DBG(CONN,
            "The peer_sta_address: " FMT_MAC ", the ifp mac is: " FMT_MAC
            ", the seq_num is %u, the status_code is %u",
            FMT_MAC_ARGS(frame->peer_sta_address), FMT_MAC_ARGS(ifp->mac_addr), frame->seq_num,
            frame->status_code);

  // Fill the authentication frame header fields.
  sae_frame->auth_hdr.auth_algorithm_number = BRCMF_AUTH_MODE_SAE;
  sae_frame->auth_hdr.auth_txn_seq_number = frame->seq_num;
  sae_frame->auth_hdr.status_code = frame->status_code;

  BRCMF_DBG(CONN, "auth_algorithm_number: %u, auth_txn_seq_number: %u, status_code: %u",
            sae_frame->auth_hdr.auth_algorithm_number, sae_frame->auth_hdr.auth_txn_seq_number,
            sae_frame->auth_hdr.status_code);

  // Attach SAE payload after authentication frame header.
  memcpy(sae_frame->sae_payload, frame->sae_fields_list, frame->sae_fields_count);

  err = brcmf_fil_iovar_data_set(ifp, "assoc_mgr_cmd", cmd_buf, cmd_buf_len, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Error sending SAE auth frame. err: %s, fw_err: %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    brcmf_return_assoc_result(ndev, STATUS_CODE_REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED);
  }

  return err;
}

zx_status_t brcmf_if_set_multicast_promisc(net_device* ndev, bool enable) {
  BRCMF_IFDBG(WLANIF, ndev, "%s promiscuous mode", enable ? "Enabling" : "Disabling");
  ndev->multicast_promisc = enable;
  brcmf_netdev_set_allmulti(ndev);
  return ZX_OK;
}

void brcmf_if_start_capture_frames(net_device* ndev,
                                   const wlan_fullmac_start_capture_frames_req_t* req,
                                   wlan_fullmac_start_capture_frames_resp_t* resp) {
  BRCMF_ERR("start_capture_frames not supported");
  resp->status = ZX_ERR_NOT_SUPPORTED;
  resp->supported_mgmt_frames = 0;
}

void brcmf_if_stop_capture_frames(net_device* ndev) {
  BRCMF_ERR("stop_capture_frames not supported");
}

static void brcmf_if_convert_ac_param(const edcf_acparam_t* acparam,
                                      wlan_wmm_ac_params_t* out_ac_params) {
  out_ac_params->aifsn = acparam->aci & EDCF_AIFSN_MASK;
  out_ac_params->acm = (acparam->aci & EDCF_ACM_MASK) != 0;
  out_ac_params->ecw_min = acparam->ecw & EDCF_ECWMIN_MASK;
  out_ac_params->ecw_max = (acparam->ecw & EDCF_ECWMAX_MASK) >> EDCF_ECWMAX_SHIFT;
  out_ac_params->txop_limit = acparam->txop;
}

void brcmf_if_wmm_status_req(net_device* ndev) {
  zx_status_t status = ZX_OK;
  bcme_status_t fw_err = BCME_OK;
  edcf_acparam_t ac_params[AC_COUNT];
  wlan_wmm_params_t resp;
  brcmf_if* ifp = ndev_to_if(ndev);

  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- ignoring wmm status req");
    return;
  }

  if (ifp == nullptr) {
    BRCMF_ERR("ifp is null");
    wlan_fullmac_impl_ifc_on_wmm_status_resp(&ndev->if_proto, ZX_ERR_INTERNAL, &resp);
    return;
  }

  status = brcmf_fil_iovar_data_get(ifp, "wme_ac_sta", &ac_params, sizeof(ac_params), &fw_err);
  // TODO(fxbug.dev/67821): Check what happens when WMM is not enabled.
  if (status != ZX_OK) {
    BRCMF_ERR("could not get STA WMM status: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    wlan_fullmac_impl_ifc_on_wmm_status_resp(&ndev->if_proto, status, &resp);
    return;
  }

  uint32_t apsd = 0;
  status = brcmf_fil_iovar_data_get(ifp, "wme_apsd", &apsd, sizeof(apsd), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("could not get WMM APSD: %s, fw err %s", zx_status_get_string(status),
              brcmf_fil_get_errstr(fw_err));
    wlan_fullmac_impl_ifc_on_wmm_status_resp(&ndev->if_proto, status, &resp);
    return;
  }

  resp.apsd = apsd != 0;
  brcmf_if_convert_ac_param(&ac_params[AC_BE], &resp.ac_be_params);
  brcmf_if_convert_ac_param(&ac_params[AC_BK], &resp.ac_bk_params);
  brcmf_if_convert_ac_param(&ac_params[AC_VI], &resp.ac_vi_params);
  brcmf_if_convert_ac_param(&ac_params[AC_VO], &resp.ac_vo_params);

  wlan_fullmac_impl_ifc_on_wmm_status_resp(&ndev->if_proto, status, &resp);
}

zx_status_t brcmf_alloc_vif(struct brcmf_cfg80211_info* cfg, uint16_t type,
                            struct brcmf_cfg80211_vif** vif_out) {
  struct brcmf_cfg80211_vif* vif_walk;
  struct brcmf_cfg80211_vif* vif;
  bool mbss;

  BRCMF_DBG(TRACE, "allocating virtual interface (size=%zu)", sizeof(*vif));
  vif = static_cast<decltype(vif)>(calloc(1, sizeof(*vif)));
  if (!vif) {
    if (vif_out) {
      *vif_out = nullptr;
    }
    return ZX_ERR_NO_MEMORY;
  }

  vif->wdev.iftype = type;
  vif->saved_ie.assoc_req_ie_len = 0;

  brcmf_init_prof(&vif->profile);

  if (type == WLAN_MAC_ROLE_AP) {
    mbss = false;
    list_for_every_entry (&cfg->vif_list, vif_walk, struct brcmf_cfg80211_vif, list) {
      if (vif_walk->wdev.iftype == WLAN_MAC_ROLE_AP) {
        mbss = true;
        break;
      }
    }
    vif->mbss = mbss;
  }

  list_add_tail(&cfg->vif_list, &vif->list);
  if (vif_out) {
    *vif_out = vif;
  }
  return ZX_OK;
}

void brcmf_free_vif(struct brcmf_cfg80211_vif* vif) {
  list_delete(&vif->list);
  free(vif);
}

void brcmf_free_net_device_vif(struct net_device* ndev) {
  struct brcmf_cfg80211_vif* vif = ndev_to_vif(ndev);

  if (vif) {
    brcmf_free_vif(vif);
  }
}

// Returns true if client is connected (also includes CONNECTING and DISCONNECTING).
static bool brcmf_is_client_connected(brcmf_if* ifp) {
  return (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state) ||
          brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state) ||
          brcmf_test_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state));
}

static const char* brcmf_get_client_connect_state_string(brcmf_if* ifp) {
  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
    return "Connected";
  }
  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state)) {
    return "Connecting";
  }
  if (brcmf_test_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state)) {
    return "Disconnecting";
  }
  return "Not connected";
}

static void brcmf_clear_assoc_ies(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_cfg80211_connect_info* conn_info = cfg_to_conn(cfg);

  free(conn_info->req_ie);
  conn_info->req_ie = nullptr;
  conn_info->req_ie_len = 0;
  free(conn_info->resp_ie);
  conn_info->resp_ie = nullptr;
  conn_info->resp_ie_len = 0;
}

static zx_status_t brcmf_get_assoc_ies(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp) {
  struct brcmf_cfg80211_assoc_ielen_le* assoc_info;
  struct brcmf_cfg80211_connect_info* conn_info = cfg_to_conn(cfg);
  uint32_t req_len;
  uint32_t resp_len;
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  brcmf_clear_assoc_ies(cfg);
  err = brcmf_fil_iovar_data_get(ifp, "assoc_info", cfg->extra_buf, WL_ASSOC_INFO_MAX, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("could not get assoc info: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    return err;
  }
  assoc_info = (struct brcmf_cfg80211_assoc_ielen_le*)cfg->extra_buf;
  req_len = assoc_info->req_len;
  resp_len = assoc_info->resp_len;
  if (req_len) {
    err =
        brcmf_fil_iovar_data_get(ifp, "assoc_req_ies", cfg->extra_buf, WL_ASSOC_INFO_MAX, &fw_err);
    if (err != ZX_OK) {
      BRCMF_ERR("could not get assoc req: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
      return err;
    }
    conn_info->req_ie_len = req_len;
    conn_info->req_ie = static_cast<decltype(conn_info->req_ie)>(
        brcmu_alloc_and_copy(cfg->extra_buf, conn_info->req_ie_len));
    if (conn_info->req_ie == nullptr) {
      conn_info->req_ie_len = 0;
    }
  } else {
    conn_info->req_ie_len = 0;
    conn_info->req_ie = nullptr;
  }
  if (resp_len) {
    err =
        brcmf_fil_iovar_data_get(ifp, "assoc_resp_ies", cfg->extra_buf, WL_ASSOC_INFO_MAX, &fw_err);
    if (err != ZX_OK) {
      BRCMF_ERR("could not get assoc resp: %s, fw err %s", zx_status_get_string(err),
                brcmf_fil_get_errstr(fw_err));
      return err;
    }
    conn_info->resp_ie_len = resp_len;
    conn_info->resp_ie = static_cast<decltype(conn_info->resp_ie)>(
        brcmu_alloc_and_copy(cfg->extra_buf, conn_info->resp_ie_len));
    if (conn_info->resp_ie == nullptr) {
      conn_info->resp_ie_len = 0;
    }
  } else {
    conn_info->resp_ie_len = 0;
    conn_info->resp_ie = nullptr;
  }
  BRCMF_DBG(CONN, "req len (%d) resp len (%d)", conn_info->req_ie_len, conn_info->resp_ie_len);
  return err;
}

// Notify SME of channel switch
zx_status_t brcmf_notify_channel_switch(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                        void* data) {
  if (!ifp) {
    return ZX_ERR_INVALID_ARGS;
  }
  struct net_device* ndev = ifp->ndev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping channel switch callback");
    return ZX_ERR_INVALID_ARGS;
  }

  uint16_t chanspec = 0;
  uint8_t ctl_chan;
  wlan_fullmac_channel_switch_info_t info;
  zx_status_t err = ZX_OK;
  struct brcmf_cfg80211_info* cfg = nullptr;
  struct wireless_dev* wdev = nullptr;

  if (e != nullptr) {
    BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  }

  cfg = ifp->drvr->config;
  wdev = ndev_to_wdev(ndev);

  // For client IF, ensure it is connected.
  if (wdev->iftype == WLAN_MAC_ROLE_CLIENT) {
    // Status should be connected.
    if (!brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
      BRCMF_ERR("CSA on %s. Not associated.", ndev->name);
      return ZX_ERR_BAD_STATE;
    }
  }
  if ((err = brcmf_get_ctrl_channel(ifp, &chanspec, &ctl_chan)) != ZX_OK) {
    return err;
  }
  BRCMF_DBG(CONN, "Channel switch ind IF: %d chanspec: 0x%x control channel: %d", ifp->ifidx,
            chanspec, ctl_chan);
  info.new_channel = ctl_chan;

  // Inform wlanif of the channel switch.
  wlan_fullmac_impl_ifc_on_channel_switch(&ndev->if_proto, &info);
  return ZX_OK;
}

static zx_status_t brcmf_notify_ap_started(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                           void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  return brcmf_notify_channel_switch(ifp, e, data);
}

static zx_status_t brcmf_notify_start_auth(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                           void* data) {
  struct net_device* ndev = ifp->ndev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping SAE auth start notifications.");
    return ZX_ERR_BAD_HANDLE;
  }
  assoc_mgr_cmd_t cmd;
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  wlan_fullmac_sae_handshake_ind_t ind;
  brcmf_ext_auth* auth_start_evt = (brcmf_ext_auth*)data;

  if (!brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state)) {
    BRCMF_INFO("Receiving a BRCMF_E_START_AUTH event when we are not even connecting to an AP.");
    return ZX_ERR_BAD_STATE;
  }

  BRCMF_DBG(EVENT,
            "The peer addr received from data is: " FMT_MAC ", the addr in event_msg is: " FMT_MAC
            "\n",
            FMT_MAC_ARGS(auth_start_evt->bssid), FMT_MAC_ARGS(e->addr));
  memcpy(ind.peer_sta_address, &auth_start_evt->bssid, ETH_ALEN);

  // SAE four-way authentication start.
  brcmf_set_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state);

  // Issue assoc_mgr_cmd to update the the state machine of firmware, so that the firmware will wait
  // for SAE frame from external supplicant.
  cmd.version = ASSOC_MGR_CURRENT_VERSION;
  cmd.length = sizeof(cmd);
  cmd.cmd = ASSOC_MGR_CMD_PAUSE_ON_EVT;
  cmd.params = ASSOC_MGR_PARAMS_PAUSE_EVENT_AUTH_RESP;
  err = brcmf_fil_iovar_data_set(ifp, "assoc_mgr_cmd", &cmd, sizeof(cmd), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Set assoc_mgr_cmd fail. err: %s, fw_err: %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
  wlan_fullmac_impl_ifc_sae_handshake_ind(&ndev->if_proto, &ind);

  return err;
}

static zx_status_t brcmf_rx_auth_frame(struct brcmf_if* ifp, const uint32_t datalen, void* data) {
  struct net_device* ndev = ifp->ndev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping SAE auth frame receive handler.");
    return ZX_ERR_BAD_HANDLE;
  }

  wlan_fullmac_sae_frame_t frame = {};
  auto pframe = (uint8_t*)data;
  auto pframe_hdr = reinterpret_cast<wlan::Authentication*>(pframe);

  BRCMF_DBG(TRACE, "Receive SAE authentication frame.");
  BRCMF_DBG(CONN, "SAE authentication frame: ");
  BRCMF_DBG(CONN, " status code: %u", pframe_hdr->status_code);
  BRCMF_DBG(CONN, " sequence number: %u", pframe_hdr->auth_txn_seq_number);

  // Copy authentication frame header information.
  memcpy(frame.peer_sta_address, &ifp->connect_req.selected_bss.bssid, ETH_ALEN);
  frame.status_code = pframe_hdr->status_code;
  frame.seq_num = pframe_hdr->auth_txn_seq_number;

  // Copy challenge text to sae_fields.
  frame.sae_fields_count = datalen - sizeof(wlan::Authentication);
  frame.sae_fields_list = pframe + sizeof(wlan::Authentication);

  // Sending SAE authentication up to SME, not rx from SME.
  wlan_fullmac_impl_ifc_sae_frame_rx(&ndev->if_proto, &frame);
  return ZX_OK;
}

static void brcmf_log_conn_status(brcmf_if* ifp, brcmf_connect_status_t connect_status) {
  BRCMF_DBG(CONN, "connect_status %s", brcmf_get_connect_status_str(connect_status));

  // We track specific failures that are of interest on inspect.
  switch (connect_status) {
    case brcmf_connect_status_t::CONNECTED:
      ifp->drvr->device->GetInspect()->LogConnSuccess();
      break;
    case brcmf_connect_status_t::AUTHENTICATION_FAILED:
      ifp->drvr->device->GetInspect()->LogConnAuthFail();
      break;
    case brcmf_connect_status_t::NO_NETWORK:
      ifp->drvr->device->GetInspect()->LogConnNoNetworkFail();
      break;
    default:
      ifp->drvr->device->GetInspect()->LogConnOtherFail();
      break;
  }
}

// This function issues BRCMF_C_DISASSOC command to firmware for cleaning firmware and AP connection
// states, firmware will send out deauth or disassoc frame to the AP based on current connection
// state.
static zx_status_t brcmf_clear_firmware_connection_state(brcmf_if* ifp) {
  zx_status_t status = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  struct brcmf_scb_val_le scbval;
  memcpy(&scbval.ea, ifp->connect_req.selected_bss.bssid, ETH_ALEN);
  scbval.val = static_cast<uint32_t>(wlan_ieee80211::ReasonCode::STA_LEAVING);
  brcmf_set_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state);
  status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_DISASSOC, &scbval, sizeof(scbval), &fw_err);
  if (status != ZX_OK) {
    BRCMF_ERR("Failed to issue BRCMF_C_DISASSOC to firmware: %s, fw err %s",
              zx_status_get_string(status), brcmf_fil_get_errstr(fw_err));
  }
  brcmf_clear_bit(brcmf_vif_status_bit_t::DISCONNECTING, &ifp->vif->sme_state);
  status = brcmf_bss_reset(ifp);
  return status;
}

static zx_status_t brcmf_bss_connect_done(brcmf_if* ifp, brcmf_connect_status_t connect_status,
                                          status_code_t assoc_result) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct net_device* ndev = ifp->ndev;
  BRCMF_DBG(TRACE, "Enter");

  if (brcmf_test_and_clear_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state)) {
    // Stop connect timer no matter connect success or not, this timer only timeout when nothing
    // is heard from firmware.
    cfg->connect_timer->Stop();
    brcmf_log_conn_status(ifp, connect_status);

    switch (connect_status) {
      case brcmf_connect_status_t::CONNECTED: {
        brcmf_get_assoc_ies(cfg, ifp);
        brcmf_set_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state);
        if (!brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFG)) {
          // Start the signal report timer
          cfg->connect_log_cnt = 0;
          cfg->signal_report_timer->Start(BRCMF_SIGNAL_REPORT_TIMER_DUR_MS);
          // Indicate the rssi soon after connection
          cfg80211_signal_ind(ndev);
        }
        brcmf_return_assoc_result(ndev, STATUS_CODE_SUCCESS);
        break;
      }
      case brcmf_connect_status_t::ASSOC_REQ_FAILED: {
        BRCMF_INFO("Association is rejected, need to reset firmware state.");
        zx_status_t err = brcmf_clear_firmware_connection_state(ifp);
        if (err != ZX_OK) {
          BRCMF_ERR("Failed to clear firmware connection state.");
        }
        brcmf_return_assoc_result(ndev, assoc_result);
        break;
      }
      default: {
        BRCMF_WARN("Unsuccessful connection: connect_status %s, assoc_result %d",
                   brcmf_get_connect_status_str(connect_status), static_cast<int>(assoc_result));
        zx_status_t err = brcmf_clear_firmware_connection_state(ifp);
        if (err != ZX_OK) {
          BRCMF_ERR("Failed to clear firmware connection state.");
        }
        brcmf_return_assoc_result(ndev, assoc_result);
        break;
      }
    }
  }

  BRCMF_DBG(TRACE, "Exit");
  return ZX_OK;
}

static void brcmf_connect_timeout_worker(WorkItem* work) {
  struct brcmf_cfg80211_info* cfg =
      containerof(work, struct brcmf_cfg80211_info, connect_timeout_work);
  struct brcmf_if* ifp = cfg_to_if(cfg);
  BRCMF_WARN(
      "Connection timeout, sending BRCMF_C_DISASSOC to firmware for state clean-up, and sending "
      "assoc result to SME.");
  zx_status_t err = brcmf_clear_firmware_connection_state(ifp);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to clear firmware connection state.");
  }
  // In case the timeout happens in SAE process.
  brcmf_clear_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state);
  brcmf_bss_connect_done(ifp, brcmf_connect_status_t::CONNECTING_TIMEOUT,
                         STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
}

static zx_status_t brcmf_indicate_client_connect(struct brcmf_if* ifp,
                                                 const struct brcmf_event_msg* e, void* data) {
  zx_status_t err = ZX_OK;

  BRCMF_DBG(TRACE, "Enter\n");
  BRCMF_DBG(CONN, "Connect Event %d, status %s reason %d auth %s flags 0x%x\n", e->event_code,
            brcmf_fweh_get_event_status_str(e->status), e->reason,
            brcmf_fweh_get_auth_type_str(e->auth_type), e->flags);
  BRCMF_DBG(CONN, "Linkup\n");

  brcmf_bss_connect_done(ifp, brcmf_connect_status_t::CONNECTED, STATUS_CODE_SUCCESS);
  brcmf_net_setcarrier(ifp, true);

  BRCMF_DBG(TRACE, "Exit\n");
  return err;
}

// Handler for ASSOC event (client only)
static zx_status_t brcmf_handle_assoc_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                            void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  ZX_DEBUG_ASSERT(!brcmf_is_apmode(ifp->vif));

  // For this event, e->reason is in the StatusCode enum space.
  status_code_t reason_code = e->reason;

  // Vendor confirmed the firmware can return reason_code 0 while status_code > 0. See
  // http://b/201803254#comment12. This is a design that they would like to not change in the
  // firmware.
  if ((BRCMF_E_STATUS_SUCCESS != e->status) && (STATUS_CODE_SUCCESS == reason_code)) {
    BRCMF_INFO(
        "Reason is SUCCESS(%u) while status indicates error: %u. Overriding reason to "
        "REFUSED_REASON_UNSPECIFIED(%u).",
        STATUS_CODE_SUCCESS, e->status, STATUS_CODE_REFUSED_REASON_UNSPECIFIED);
    reason_code = STATUS_CODE_REFUSED_REASON_UNSPECIFIED;
  }

  return brcmf_bss_connect_done(ifp,
                                (e->status == BRCMF_E_STATUS_SUCCESS)
                                    ? brcmf_connect_status_t::CONNECTED
                                    : brcmf_connect_status_t::ASSOC_REQ_FAILED,
                                reason_code);
}

// Handler to ASSOC_IND and REASSOC_IND events. These are explicitly meant for SoftAP
static zx_status_t brcmf_handle_assoc_ind(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                          void* data) {
  struct net_device* ndev = ifp->ndev;
  std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
  if (ndev->if_proto.ops == nullptr) {
    BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping assoc ind callback");
    return ZX_OK;
  }

  BRCMF_DBG(EVENT, "IF: %d event %s (%u) status %s reason %d auth %s flags 0x%x", ifp->ifidx,
            brcmf_fweh_event_name(static_cast<brcmf_fweh_event_code>(e->event_code)), e->event_code,
            brcmf_fweh_get_event_status_str(e->status), e->reason,
            brcmf_fweh_get_auth_type_str(e->auth_type), e->flags);
  ZX_DEBUG_ASSERT(brcmf_is_apmode(ifp->vif));

  if (e->reason != BRCMF_E_STATUS_SUCCESS) {
    return ZX_OK;
  }

  if (data == nullptr || e->datalen == 0) {
    BRCMF_ERR("Received ASSOC_IND with no IEs");
    return ZX_ERR_INVALID_ARGS;
  }

  const struct brcmf_tlv* ssid_ie = brcmf_parse_tlvs(data, e->datalen, WLAN_IE_TYPE_SSID);
  if (ssid_ie == nullptr) {
    BRCMF_ERR("Received ASSOC_IND with no SSID IE");
    return ZX_ERR_INVALID_ARGS;
  }

  if (ssid_ie->len > wlan_ieee80211::MAX_SSID_BYTE_LEN) {
    BRCMF_ERR("Received ASSOC_IND with invalid SSID IE");
    return ZX_ERR_INVALID_ARGS;
  }

  const struct brcmf_tlv* rsn_ie = brcmf_parse_tlvs(data, e->datalen, WLAN_IE_TYPE_RSNE);
  if (rsn_ie && rsn_ie->len > fuchsia::wlan::ieee80211::WLAN_IE_BODY_MAX_LEN) {
    BRCMF_ERR("Received ASSOC_IND with invalid RSN IE");
    return ZX_ERR_INVALID_ARGS;
  }

  wlan_fullmac_assoc_ind_t assoc_ind_params;
  memset(&assoc_ind_params, 0, sizeof(assoc_ind_params));
  memcpy(assoc_ind_params.peer_sta_address, e->addr, ETH_ALEN);

  // Unfortunately, we have to ask the firmware to provide the associated station's
  // listen interval.
  struct brcmf_sta_info_le sta_info;
  uint8_t mac[ETH_ALEN];
  memcpy(mac, e->addr, ETH_ALEN);
  brcmf_cfg80211_get_station(ndev, mac, &sta_info);
  // convert from ms to beacon periods
  assoc_ind_params.listen_interval =
      sta_info.listen_interval_inms / ifp->vif->profile.beacon_period;

  // Extract the SSID from the IEs
  assoc_ind_params.ssid.len = ssid_ie->len;
  memcpy(assoc_ind_params.ssid.data, ssid_ie->data, ssid_ie->len);

  // Extract the RSN information from the IEs
  if (rsn_ie != nullptr) {
    assoc_ind_params.rsne_len = rsn_ie->len + TLV_HDR_LEN;
    memcpy(assoc_ind_params.rsne, rsn_ie, assoc_ind_params.rsne_len);
  }

  BRCMF_IFDBG(WLANIF, ndev, "Sending assoc indication to SME.");
#if !defined(NDEBUG)
  BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC "",
              FMT_MAC_ARGS(assoc_ind_params.peer_sta_address));
#endif /* !defined(NDEBUG) */

  wlan_fullmac_impl_ifc_assoc_ind(&ndev->if_proto, &assoc_ind_params);
  return ZX_OK;
}

// Prints extra information about auth events, based on information obtained from the vendor.
static void brcmf_print_auth_event_details(const struct brcmf_event_msg* e) {
  switch (e->status) {
    case BRCMF_E_STATUS_SUCCESS:
      // Nothing to see here
      break;
    case BRCMF_E_STATUS_NO_ACK:
      // Vendor reported the timeout is 300ms within firmware and is not configurable.
      // Typical acks happen in approx 1ms, so this timeout should be more than long enough.
      BRCMF_WARN("No MAC-level ack received for auth request within 300ms firmware timeout");
      break;
    case BRCMF_E_STATUS_TIMEOUT:
      // Vendor reported the timeout is 300ms within firmware and is not configurable. Note
      // this timeout is separate from the timeout for BRCMF_E_STATUS_NO_ACK.
      BRCMF_WARN("No auth response received from AP within 300ms firmware timeout");
      break;
    case BRCMF_E_STATUS_UNSOLICITED:
      // Vendor reported this is an unexpected Ack response from AP, or an out-of-sequence auth
      // response.
      BRCMF_WARN("Received an unexpected or out-of-sequence auth response from AP");
      break;
    case BRCMF_E_STATUS_FAIL:
      // Vendor informed us that when e->event_code is 16, the e->reason corresponds to 802.11
      // deauth reason codes.
      if (e->event_code == 16) {
        BRCMF_WARN("AP rejected auth attempt, reference reason code against 802.11 deauth reasons");
      } else {
        BRCMF_ERR(
            "Unexpected event code for BRCMF_E_STATUS_FAIL, follow up with vendor for more "
            "information");
      }
      break;
    case BRCMF_E_STATUS_ABORT:
      // This status occurs very rarely and transiently. We haven't confirmed the meaning with
      // the vendor, but it's not a cause for concern (see https://fxbug.dev/101906#c17).
      break;
    default:
      BRCMF_ERR("Unexpected status, follow up with vendor for more information");
  }
}

// Handler for AUTH event (client only)
static zx_status_t brcmf_process_auth_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                            void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return static_cast<int>(reason); });

  ZX_DEBUG_ASSERT(!brcmf_is_apmode(ifp->vif));

  if (e->status != BRCMF_E_STATUS_SUCCESS) {
    BRCMF_INFO("Auth Failure auth %s status %s reason %d flags 0x%x event %lu",
               brcmf_fweh_get_auth_type_str(e->auth_type),
               brcmf_fweh_get_event_status_str(e->status), static_cast<int>(e->reason), e->flags,
               static_cast<unsigned long>(e->event_code));
    brcmf_print_auth_event_details(e);

    // It appears FW continues to be busy with authentication when this event is received
    // specifically with WEP. Attempt to shutdown the IF.
    brcmf_bss_reset(ifp);

    if (brcmf_test_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state)) {
      // Issue assoc_mgr_cmd to resume firmware from waiting for the success of SAE authentication.
      bcme_status_t fwerr = BCME_OK;
      zx_status_t status;
      assoc_mgr_cmd_t cmd;

      cmd.version = ASSOC_MGR_CURRENT_VERSION;
      cmd.length = sizeof(cmd);
      cmd.cmd = ASSOC_MGR_CMD_PAUSE_ON_EVT;
      cmd.params = ASSOC_MGR_PARAMS_EVENT_NONE;

      status = brcmf_fil_iovar_data_set(ifp, "assoc_mgr_cmd", &cmd, sizeof(cmd), &fwerr);
      if (status != ZX_OK) {
        // An error will always be returned here until the firmware bug is fixed.
        // TODO(zhiyichen): Remove the comment once the firmware bug is fixed.
        BRCMF_ERR("Set iovar assoc_mgr_cmd fail. err: %s, fw_err: %s", zx_status_get_string(status),
                  brcmf_fil_get_errstr(fwerr));
      }
      brcmf_clear_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state);
    }
    brcmf_bss_connect_done(ifp, brcmf_connect_status_t::AUTHENTICATION_FAILED,
                           STATUS_CODE_REFUSED_UNAUTHENTICATED_ACCESS_NOT_SUPPORTED);
  }

  // Only care about the authentication frames during SAE process.
  if (brcmf_test_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &ifp->vif->sme_state) &&
      e->datalen > 0) {
    BRCMF_INFO("SAE frame received from driver.");
    return brcmf_rx_auth_frame(ifp, e->datalen, data);
  }

  return ZX_OK;
}

// AUTH_IND handler. AUTH_IND is meant only for SoftAP IF
static zx_status_t brcmf_process_auth_ind_event(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  ZX_DEBUG_ASSERT(brcmf_is_apmode(ifp->vif));

  if (e->reason == BRCMF_E_STATUS_SUCCESS) {
    struct net_device* ndev = ifp->ndev;
    std::shared_lock<std::shared_mutex> guard(ndev->if_proto_lock);
    if (ndev->if_proto.ops == nullptr) {
      BRCMF_IFDBG(WLANIF, ndev, "interface stopped -- skipping auth ind callback");
      return ZX_OK;
    }
    wlan_fullmac_auth_ind_t auth_ind_params;
    const char* auth_type;

    memset(&auth_ind_params, 0, sizeof(auth_ind_params));
    memcpy(auth_ind_params.peer_sta_address, e->addr, ETH_ALEN);
    // We always authenticate as an open system for WPA
    auth_ind_params.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
    switch (auth_ind_params.auth_type) {
      case WLAN_AUTH_TYPE_OPEN_SYSTEM:
        auth_type = "open";
        break;
      case WLAN_AUTH_TYPE_SHARED_KEY:
        auth_type = "shared key";
        break;
      case WLAN_AUTH_TYPE_FAST_BSS_TRANSITION:
        auth_type = "fast bss transition";
        break;
      case WLAN_AUTH_TYPE_SAE:
        auth_type = "SAE";
        break;
      default:
        auth_type = "unknown";
    }
    BRCMF_IFDBG(WLANIF, ndev, "Sending auth indication to SME. type: %s", auth_type);
#if !defined(NDEBUG)
    BRCMF_IFDBG(WLANIF, ndev, "  address: " FMT_MAC,
                FMT_MAC_ARGS(auth_ind_params.peer_sta_address));
#endif /* !defined(NDEBUG) */

    wlan_fullmac_impl_ifc_auth_ind(&ndev->if_proto, &auth_ind_params);
  }
  return ZX_OK;
}

static void brcmf_indicate_no_network(struct brcmf_if* ifp) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

  BRCMF_DBG(CONN, "No network\n");
  brcmf_bss_connect_done(ifp, brcmf_connect_status_t::NO_NETWORK,
                         STATUS_CODE_REFUSED_EXTERNAL_REASON);
  brcmf_disconnect_done(cfg);
}

static zx_status_t brcmf_indicate_client_disconnect(struct brcmf_if* ifp,
                                                    const struct brcmf_event_msg* e, void* data,
                                                    brcmf_connect_status_t connect_status) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct net_device* ndev = ifp->ndev;
  zx_status_t status = ZX_OK;

  BRCMF_DBG(TRACE, "Enter\n");
  if (!brcmf_is_client_connected(ifp)) {
    // Client is already disconnected.
    return status;
  }
  // Start of disconnect process. Reset disconnect_done.
  sync_completion_reset(&ifp->disconnect_done);

  // TODO(fxb/61311): Remove once this verbose logging is no longer needed in
  // brcmf_indicate_client_disconnect(). This log should be moved to CONN
  // for production code.
  BRCMF_INFO("client disconnect indicated. state %s, rssi, %d snr, %d",
             brcmf_get_client_connect_state_string(ifp), ndev->last_known_rssi_dbm,
             ndev->last_known_snr_db);
  BRCMF_INFO_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  brcmf_bss_connect_done(ifp, connect_status,
                         (connect_status == brcmf_connect_status_t::CONNECTED)
                             ? STATUS_CODE_SUCCESS
                             : STATUS_CODE_REFUSED_REASON_UNSPECIFIED);

  wlan_ieee80211::ReasonCode reason_code = (connect_status == brcmf_connect_status_t::LINK_FAILED)
                                               ? wlan_ieee80211::ReasonCode::MLME_LINK_FAILED
                                               : wlan::common::ConvertReasonCode(e->reason);
  brcmf_disconnect_done(cfg);
  brcmf_link_down(ifp->vif, reason_code, e->event_code);
  brcmf_clear_profile_on_client_disconnect(ndev_to_prof(ndev));
  if (ndev != cfg_to_ndev(cfg)) {
    sync_completion_signal(&cfg->vif_disabled);
  }
  brcmf_net_setcarrier(ifp, false);
  // Signal completion of disconnect process.
  BRCMF_DBG(CONN, "Indicate disconnect done");
  sync_completion_signal(&ifp->disconnect_done);
  BRCMF_DBG(TRACE, "Exit\n");
  return status;
}

static zx_status_t brcmf_process_link_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                            void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  if (brcmf_is_apmode(ifp->vif)) {
    struct net_device* ndev = ifp->ndev;
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

    // TODO(karthikrish): Confirm with vendor if flags is indeed a bitmask.
    if (!(e->flags & BRCMF_EVENT_MSG_LINK)) {
      BRCMF_DBG(CONN, "AP mode link down\n");
      sync_completion_signal(&cfg->vif_disabled);
      return ZX_OK;
    }
    BRCMF_DBG(CONN, "AP mode link up\n");
    struct brcmf_if* ifp = ndev_to_if(ndev);

    // Indicate status only if AP is in start pending state (could have been cleared if
    // a stop request comes in before this event is received).
    if (brcmf_test_and_clear_bit(brcmf_vif_status_bit_t::AP_START_PENDING, &ifp->vif->sme_state)) {
      // Stop the timer when we get a result from firmware.
      cfg->ap_start_timer->Stop();
      // confirm AP Start
      brcmf_if_start_conf(ndev, WLAN_START_RESULT_SUCCESS);
      // Set AP_CREATED
      brcmf_set_bit(brcmf_vif_status_bit_t::AP_CREATED, &ifp->vif->sme_state);
    }
  } else {
    BRCMF_DBG(CONN, "Client mode link event.");
    if (e->status == BRCMF_E_STATUS_SUCCESS && (e->flags & BRCMF_EVENT_MSG_LINK)) {
      return brcmf_indicate_client_connect(ifp, e, data);
    }
    if (!(e->flags & BRCMF_EVENT_MSG_LINK)) {
      return brcmf_indicate_client_disconnect(ifp, e, data, brcmf_connect_status_t::LINK_FAILED);
    }
    if (e->status == BRCMF_E_STATUS_NO_NETWORKS) {
      brcmf_indicate_no_network(ifp);
    }
  }
  return ZX_OK;
}

static zx_status_t brcmf_process_deauth_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                              void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
  if (brcmf_is_apmode(ifp->vif)) {
    if (e->event_code == BRCMF_E_DEAUTH_IND) {
      brcmf_notify_deauth_ind(ifp->ndev, e->addr, wlan::common::ConvertReasonCode(e->reason),
                              false);
    } else {
      // E_DEAUTH
      brcmf_notify_deauth(ifp->ndev, e->addr);
    }
    return ZX_OK;
  }

  // Sometimes FW sends E_DEAUTH when a unicast packet is received before association
  // is complete. Ignore it.
  if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTING, &ifp->vif->sme_state) &&
      e->reason == BRCMF_E_REASON_UCAST_FROM_UNASSOC_STA) {
    BRCMF_DBG(EVENT, "E_DEAUTH because data rcvd before assoc...ignore");
    return ZX_OK;
  }
  return brcmf_indicate_client_disconnect(ifp, e, data, brcmf_connect_status_t::DEAUTHENTICATING);
}

static zx_status_t brcmf_process_disassoc_ind_event(struct brcmf_if* ifp,
                                                    const struct brcmf_event_msg* e, void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
  if (brcmf_is_apmode(ifp->vif)) {
    if (e->event_code == BRCMF_E_DISASSOC_IND)
      brcmf_notify_disassoc_ind(ifp->ndev, e->addr, wlan::common::ConvertReasonCode(e->reason),
                                false);
    else
      // E_DISASSOC
      brcmf_notify_disassoc(ifp->ndev, ZX_OK);
    return ZX_OK;
  }
  return brcmf_indicate_client_disconnect(ifp, e, data, brcmf_connect_status_t::DISASSOCIATING);
}

static zx_status_t brcmf_process_set_ssid_event(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  if (e->status == BRCMF_E_STATUS_SUCCESS) {
    BRCMF_DBG(CONN, "set ssid success\n");
    memcpy(ifp->vif->profile.bssid, e->addr, ETH_ALEN);
  } else {
    BRCMF_DBG(CONN, "set ssid failed - no network found\n");
    brcmf_indicate_no_network(ifp);
  }
  return ZX_OK;
}

static zx_status_t brcmf_notify_roaming_status(struct brcmf_if* ifp,
                                               const struct brcmf_event_msg* e, void* data) {
  uint32_t event = e->event_code;
  brcmf_fweh_event_status_t status = e->status;

  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  if (event == BRCMF_E_ROAM && status == BRCMF_E_STATUS_SUCCESS) {
    if (brcmf_test_bit(brcmf_vif_status_bit_t::CONNECTED, &ifp->vif->sme_state)) {
      BRCMF_ERR("Received roaming notification - unsupported");
    } else {
      brcmf_bss_connect_done(ifp, brcmf_connect_status_t::CONNECTED, STATUS_CODE_SUCCESS);
      brcmf_net_setcarrier(ifp, true);
    }
  }

  return ZX_OK;
}

static zx_status_t brcmf_notify_mic_status(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                           void* data) {
  uint16_t flags = e->flags;
  enum nl80211_key_type key_type;

  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });

  if (flags & BRCMF_EVENT_MSG_GROUP) {
    key_type = NL80211_KEYTYPE_GROUP;
  } else {
    key_type = NL80211_KEYTYPE_PAIRWISE;
  }

  cfg80211_michael_mic_failure(ifp->ndev, (uint8_t*)&e->addr, key_type, -1, nullptr);

  return ZX_OK;
}

static zx_status_t brcmf_notify_vif_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                          void* data) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  struct brcmf_if_event* ifevent = (struct brcmf_if_event*)data;
  struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;
  struct brcmf_cfg80211_vif* vif;

  BRCMF_DBG_EVENT(ifp, e, "%d", [](uint32_t reason) { return reason; });
  BRCMF_DBG(EVENT, "IF event: action %u flags %u ifidx %u bsscfgidx %u", ifevent->action,
            ifevent->flags, ifevent->ifidx, ifevent->bsscfgidx);

  mtx_lock(&event->vif_event_lock);
  event->action = ifevent->action;
  vif = event->vif;

  switch (ifevent->action) {
    case BRCMF_E_IF_ADD:
      /* waiting process may have timed out */
      if (!cfg->vif_event.vif) {
        mtx_unlock(&event->vif_event_lock);
        return ZX_ERR_SHOULD_WAIT;
      }

      ifp->vif = vif;
      vif->ifp = ifp;
      if (ifp->ndev) {
        vif->wdev.netdev = ifp->ndev;
      }
      mtx_unlock(&event->vif_event_lock);
      if (event->action == cfg->vif_event_pending_action) {
        sync_completion_signal(&event->vif_event_wait);
      }
      return ZX_OK;

    case BRCMF_E_IF_DEL:
      mtx_unlock(&event->vif_event_lock);
      /* event may not be upon user request */
      if (brcmf_cfg80211_vif_event_armed(cfg) && event->action == cfg->vif_event_pending_action) {
        sync_completion_signal(&event->vif_event_wait);
      }
      return ZX_OK;

    case BRCMF_E_IF_CHANGE:
      mtx_unlock(&event->vif_event_lock);
      if (event->action == cfg->vif_event_pending_action) {
        sync_completion_signal(&event->vif_event_wait);
      }
      return ZX_OK;

    default:
      mtx_unlock(&event->vif_event_lock);
      break;
  }
  return ZX_ERR_INVALID_ARGS;
}

static void brcmf_init_conf(struct brcmf_cfg80211_conf* conf) {
  conf->frag_threshold = (uint32_t)-1;
  conf->rts_threshold = (uint32_t)-1;
  conf->retry_short = (uint32_t)-1;
  conf->retry_long = (uint32_t)-1;
}

static void brcmf_register_event_handlers(struct brcmf_cfg80211_info* cfg) {
  brcmf_fweh_register(cfg->pub, BRCMF_E_LINK, brcmf_process_link_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_AUTH, brcmf_process_auth_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_AUTH_IND, brcmf_process_auth_ind_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH_IND, brcmf_process_deauth_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH, brcmf_process_deauth_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_DISASSOC_IND, brcmf_process_disassoc_ind_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_DISASSOC, brcmf_process_disassoc_ind_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_ASSOC, brcmf_handle_assoc_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_ASSOC_IND, brcmf_handle_assoc_ind);
  brcmf_fweh_register(cfg->pub, BRCMF_E_REASSOC_IND, brcmf_handle_assoc_ind);
  brcmf_fweh_register(cfg->pub, BRCMF_E_ROAM, brcmf_notify_roaming_status);
  brcmf_fweh_register(cfg->pub, BRCMF_E_MIC_ERROR, brcmf_notify_mic_status);
  brcmf_fweh_register(cfg->pub, BRCMF_E_SET_SSID, brcmf_process_set_ssid_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_IF, brcmf_notify_vif_event);
  brcmf_fweh_register(cfg->pub, BRCMF_E_CSA_COMPLETE_IND, brcmf_notify_channel_switch);
  brcmf_fweh_register(cfg->pub, BRCMF_E_AP_STARTED, brcmf_notify_ap_started);
  brcmf_fweh_register(cfg->pub, BRCMF_E_JOIN_START, brcmf_notify_start_auth);
}

static void brcmf_deinit_cfg_mem(struct brcmf_cfg80211_info* cfg) {
  // Delete (which also causes a stop) all timers first. Their callbacks may use the data being
  // free'd below so we must ensure they are not called after those free calls.
  delete cfg->disconnect_timer;
  delete cfg->escan_timer;
  delete cfg->signal_report_timer;
  delete cfg->ap_start_timer;
  delete cfg->connect_timer;
  free(cfg->conf);
  cfg->conf = nullptr;
  free(cfg->extra_buf);
  cfg->extra_buf = nullptr;
  free(cfg->wowl.nd);
  cfg->wowl.nd = nullptr;
  free(cfg->wowl.nd_info);
  cfg->wowl.nd_info = nullptr;
}

static zx_status_t brcmf_init_cfg_mem(struct brcmf_cfg80211_info* cfg) {
  cfg->conf = static_cast<decltype(cfg->conf)>(calloc(1, sizeof(*cfg->conf)));
  if (!cfg->conf) {
    goto init_priv_mem_out;
  }
  cfg->extra_buf = static_cast<decltype(cfg->extra_buf)>(calloc(1, WL_EXTRA_BUF_MAX));
  if (!cfg->extra_buf) {
    goto init_priv_mem_out;
  }
  cfg->wowl.nd =
      static_cast<decltype(cfg->wowl.nd)>(calloc(1, sizeof(*cfg->wowl.nd) + sizeof(uint32_t)));
  if (!cfg->wowl.nd) {
    goto init_priv_mem_out;
  }
  cfg->wowl.nd_info = static_cast<decltype(cfg->wowl.nd_info)>(
      calloc(1, sizeof(*cfg->wowl.nd_info) + sizeof(struct cfg80211_wowlan_nd_match*)));
  if (!cfg->wowl.nd_info) {
    goto init_priv_mem_out;
  }
  return ZX_OK;

init_priv_mem_out:
  brcmf_deinit_cfg_mem(cfg);

  return ZX_ERR_NO_MEMORY;
}

static zx_status_t brcmf_init_cfg(struct brcmf_cfg80211_info* cfg) {
  zx_status_t err = ZX_OK;
  async_dispatcher_t* dispatcher = cfg->pub->device->GetDispatcher();

  cfg->scan_in_progress = false;
  cfg->dongle_up = false; /* dongle is not up yet */
  err = brcmf_init_cfg_mem(cfg);
  if (err != ZX_OK) {
    return err;
  }
  brcmf_register_event_handlers(cfg);
  mtx_init(&cfg->usr_sync, mtx_plain);
  brcmf_init_escan(cfg);
  brcmf_init_conf(cfg->conf);

  // Initialize the disconnect timer
  cfg->disconnect_timer = new Timer(dispatcher, std::bind(brcmf_disconnect_timeout, cfg), false);
  cfg->disconnect_timeout_work = WorkItem(brcmf_disconnect_timeout_worker);
  // Initialize the signal report timer
  cfg->signal_report_timer =
      new Timer(dispatcher, std::bind(brcmf_signal_report_timeout, cfg), true);
  cfg->signal_report_work = WorkItem(brcmf_signal_report_worker);
  // Initialize the ap start timer
  cfg->ap_start_timer = new Timer(dispatcher, std::bind(brcmf_ap_start_timeout, cfg), false);
  cfg->ap_start_timeout_work = WorkItem(brcmf_ap_start_timeout_worker);
  // Initialize the connect timer
  cfg->connect_timer = new Timer(dispatcher, std::bind(brcmf_connect_timeout, cfg), false);
  cfg->connect_timeout_work = WorkItem(brcmf_connect_timeout_worker);

  cfg->vif_disabled = {};
  return err;
}

static void brcmf_deinit_cfg(struct brcmf_cfg80211_info* cfg) {
  cfg->dongle_up = false; /* dongle down */
  brcmf_abort_scanning_immediately(cfg);
  brcmf_deinit_cfg_mem(cfg);
}

static void init_vif_event(struct brcmf_cfg80211_vif_event* event) {
  event->vif_event_wait = {};
  mtx_init(&event->vif_event_lock, mtx_plain);
}

static zx_status_t brcmf_dongle_roam(struct brcmf_if* ifp) {
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;
  uint32_t bcn_timeout;
  uint32_t roamtrigger[2];
  uint32_t roam_delta[2];

  if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
    return ZX_OK;  // TODO(fxbug.dev/29354) Find out why, and document.
  }
  /* Configure beacon timeout value based upon roaming setting */
  if (ifp->drvr->settings->roamoff) {
    bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_OFF;
  } else {
    bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_ON;
  }
  err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout", bcn_timeout, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("bcn_timeout error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto roam_setup_done;
  }

  /* Enable/Disable built-in roaming to allow supplicant to take care of
   * roaming.
   */
  BRCMF_INFO("Setting roam_off = %s", ifp->drvr->settings->roamoff ? "Off" : "On");
  err = brcmf_fil_iovar_int_set(ifp, "roam_off", ifp->drvr->settings->roamoff, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("roam_off error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto roam_setup_done;
  }

  roamtrigger[0] = WL_ROAM_TRIGGER_LEVEL;
  roamtrigger[1] = BRCM_BAND_ALL;
  err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_TRIGGER, roamtrigger, sizeof(roamtrigger),
                               &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("WLC_SET_ROAM_TRIGGER error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto roam_setup_done;
  }

  roam_delta[0] = WL_ROAM_DELTA;
  roam_delta[1] = BRCM_BAND_ALL;
  err =
      brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_DELTA, roam_delta, sizeof(roam_delta), &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("WLC_SET_ROAM_DELTA error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto roam_setup_done;
  }

roam_setup_done:
  return err;
}

static zx_status_t brcmf_dongle_scantime(struct brcmf_if* ifp) {
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME, BRCMF_SCAN_CHANNEL_TIME, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Scan assoc time error: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto dongle_scantime_out;
  }
  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME, BRCMF_SCAN_UNASSOC_TIME, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Scan unassoc time error %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto dongle_scantime_out;
  }

  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_PASSIVE_TIME, BRCMF_SCAN_PASSIVE_TIME, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Scan passive time error %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto dongle_scantime_out;
  }

dongle_scantime_out:
  return err;
}

static zx_status_t brcmf_enable_bw40_2g(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_if* ifp = cfg_to_if(cfg);
  struct brcmf_fil_bwcap_le band_bwcap;
  uint32_t val;
  zx_status_t err;

  /* verify support for bw_cap command */
  val = WLC_BAND_5G;
  err = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);

  if (err == ZX_OK) {
    /* only set 2G bandwidth using bw_cap command */
    band_bwcap.band = WLC_BAND_2G;
    band_bwcap.bw_cap = WLC_BW_CAP_40MHZ;
    err = brcmf_fil_iovar_data_set(ifp, "bw_cap", &band_bwcap, sizeof(band_bwcap), nullptr);
  } else {
    BRCMF_DBG(FIL, "Falling back to mimo_bw_cap to set 40MHz bandwidth for 2.4GHz bands.");
    val = WLC_N_BW_40ALL;
    err = brcmf_fil_iovar_int_set(ifp, "mimo_bw_cap", val, nullptr);
  }

  return err;
}

static zx_status_t brcmf_config_dongle(struct brcmf_cfg80211_info* cfg) {
  struct net_device* ndev;
  struct wireless_dev* wdev;
  struct brcmf_if* ifp;
  zx_status_t err = ZX_OK;
  bool enable_arp_nd_offload;

  BRCMF_DBG(TEMP, "Enter");
  if (cfg->dongle_up) {
    BRCMF_DBG(TEMP, "Early done");
    return err;
  }

  ndev = cfg_to_ndev(cfg);
  wdev = ndev_to_wdev(ndev);
  ifp = ndev_to_if(ndev);

  /* make sure RF is ready for work */
  brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 0, nullptr);

  brcmf_dongle_scantime(ifp);

  err = brcmf_dongle_roam(ifp);
  if (err != ZX_OK) {
    goto default_conf_out;
  }

  // Disable Wireless Network Management (802.11v) firmware offload.
  bcme_status_t fwerr;
  err = brcmf_fil_iovar_int_set(ifp, "wnm", 0, &fwerr);
  if (err == ZX_OK || err == ZX_ERR_NOT_SUPPORTED) {
    // Note: if iovar is not supported, then WNM is effectively disabled.
    BRCMF_DBG(FIL, "WNM is disabled in firmware");
  } else {
    // Proceed even if WNM could not be disabled.
    BRCMF_WARN("Could not disable WNM, firmware error %s", brcmf_fil_get_errstr(fwerr));
  }

  err = brcmf_cfg80211_change_iface_mac_role(cfg, ndev, wdev->iftype);
  if (err != ZX_OK) {
    goto default_conf_out;
  }

  enable_arp_nd_offload = !brcmf_feat_is_enabled(ifp, BRCMF_FEAT_AP);
  brcmf_configure_arp_nd_offload(ifp, enable_arp_nd_offload);

  cfg->dongle_up = true;
default_conf_out:
  BRCMF_DBG(TEMP, "Returning %d", err);

  return err;
}

static zx_status_t __brcmf_cfg80211_up(struct brcmf_if* ifp) {
  brcmf_set_bit(brcmf_vif_status_bit_t::READY, &ifp->vif->sme_state);

  return brcmf_config_dongle(ifp->drvr->config);
}

static zx_status_t __brcmf_cfg80211_down(struct brcmf_if* ifp) {
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  /*
   * While going down, if associated with AP disassociate
   * from AP to save power
   */
  if (check_vif_up(ifp->vif)) {
    brcmf_link_down(ifp->vif, wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON, 0);

    /* Make sure WPA_Supplicant receives all the event
       generated due to DISASSOC call to the fw to keep
       the state fw and WPA_Supplicant state consistent
     */
    msleep(500);
  }

  brcmf_abort_scanning_immediately(cfg);
  brcmf_clear_bit(brcmf_vif_status_bit_t::READY, &ifp->vif->sme_state);

  return ZX_OK;
}

zx_status_t brcmf_cfg80211_up(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  zx_status_t err = ZX_OK;

  mtx_lock(&cfg->usr_sync);
  err = __brcmf_cfg80211_up(ifp);
  mtx_unlock(&cfg->usr_sync);

  return err;
}

zx_status_t brcmf_cfg80211_down(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
  zx_status_t err = ZX_OK;

  mtx_lock(&cfg->usr_sync);
  err = __brcmf_cfg80211_down(ifp);
  mtx_unlock(&cfg->usr_sync);

  return err;
}

uint16_t brcmf_cfg80211_get_iftype(struct brcmf_if* ifp) {
  struct wireless_dev* wdev = &ifp->vif->wdev;

  return wdev->iftype;
}

const char* brcmf_cfg80211_get_iface_str(struct net_device* ndev) {
  if (ndev_to_vif(ndev)->wdev.iftype == WLAN_MAC_ROLE_CLIENT)
    return "Client";
  else
    return "SoftAP";
}

bool brcmf_get_vif_state_any(struct brcmf_cfg80211_info* cfg, brcmf_vif_status_bit_t state) {
  struct brcmf_cfg80211_vif* vif;

  list_for_every_entry (&cfg->vif_list, vif, struct brcmf_cfg80211_vif, list) {
    if (brcmf_test_bit(state, &vif->sme_state)) {
      return true;
    }
  }
  return false;
}

void brcmf_cfg80211_arm_vif_event(struct brcmf_cfg80211_info* cfg, struct brcmf_cfg80211_vif* vif,
                                  uint8_t pending_action) {
  struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

  mtx_lock(&event->vif_event_lock);
  event->vif = vif;
  event->action = 0;
  sync_completion_reset(&event->vif_event_wait);
  cfg->vif_event_pending_action = pending_action;
  mtx_unlock(&event->vif_event_lock);
}

void brcmf_cfg80211_disarm_vif_event(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

  mtx_lock(&event->vif_event_lock);
  event->vif = nullptr;
  event->action = 0;
  mtx_unlock(&event->vif_event_lock);
}

bool brcmf_cfg80211_vif_event_armed(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;
  bool armed;

  mtx_lock(&event->vif_event_lock);
  armed = event->vif != nullptr;
  mtx_unlock(&event->vif_event_lock);

  return armed;
}

zx_status_t brcmf_cfg80211_wait_vif_event(struct brcmf_cfg80211_info* cfg, zx_duration_t timeout) {
  struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

  return sync_completion_wait(&event->vif_event_wait, timeout);
}

zx_status_t brcmf_cfg80211_del_iface(struct brcmf_cfg80211_info* cfg, struct wireless_dev* wdev) {
  struct net_device* ndev = wdev->netdev;
  struct brcmf_if* ifp = ndev_to_if(ndev);

  /* vif event pending in firmware */
  if (brcmf_cfg80211_vif_event_armed(cfg)) {
    return ZX_ERR_UNAVAILABLE;
  }

  if (ndev) {
    if (brcmf_test_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status) &&
        cfg->escan_info.ifp == ndev_to_if(ndev)) {
      BRCMF_WARN("Aborting scan, interface being removed");
      brcmf_abort_scanning_immediately(cfg);
    }

    struct brcmf_if* client_ifp = cfg_to_if(cfg);
    brcmf_enable_mpc(client_ifp, 1);
  }

  zx_status_t err = brcmf_bus_flush_txq(ifp->drvr->bus_if, ifp->ifidx);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to flush TXQ on iface %d: %s", ifp->ifidx, zx_status_get_string(err));
  }

  switch (wdev->iftype) {
    case WLAN_MAC_ROLE_AP:
      // Stop the AP in an attempt to exit gracefully.
      brcmf_cfg80211_stop_ap(ndev);
      ndev->mlme_channel.reset();
      return brcmf_cfg80211_del_ap_iface(cfg, wdev);
    case WLAN_MAC_ROLE_CLIENT:
      // Dissconnect the client in an attempt to exit gracefully.
      brcmf_link_down(ifp->vif, wlan_ieee80211::ReasonCode::UNSPECIFIED_REASON, false);
      // The default client iface 0 is always assumed to exist by the driver, and is never
      // explicitly deleted.
      ndev->mlme_channel.reset();
      ndev->needs_free_net_device = true;
      brcmf_write_net_device_name(ndev, kPrimaryNetworkInterfaceName);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t brcmf_cfg80211_attach(struct brcmf_pub* drvr) {
  struct brcmf_cfg80211_info* cfg;
  struct brcmf_cfg80211_vif* vif;
  struct brcmf_if* ifp = brcmf_get_ifp(drvr, 0);
  struct net_device* ndev = ifp->ndev;
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;
  int32_t io_type;

  BRCMF_DBG(TEMP, "Enter");
  if (!ndev) {
    BRCMF_ERR("ndev is invalid");
    return ZX_ERR_UNAVAILABLE;
  }

  cfg = static_cast<decltype(cfg)>(calloc(1, sizeof(struct brcmf_cfg80211_info)));
  if (cfg == nullptr) {
    goto cfg80211_info_out;
  }

  cfg->pub = drvr;
  init_vif_event(&cfg->vif_event);
  list_initialize(&cfg->vif_list);
  err = brcmf_alloc_vif(cfg, WLAN_MAC_ROLE_CLIENT, &vif);
  if (err != ZX_OK) {
    goto cfg80211_info_out;
  }

  vif->ifp = ifp;
  vif->wdev.netdev = ndev;

  err = brcmf_init_cfg(cfg);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to init cfg (%d)", err);
    brcmf_free_vif(vif);
    goto cfg80211_info_out;
  }
  ifp->vif = vif;

  /* determine d11 io type before wiphy setup */
  err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_VERSION, (uint32_t*)&io_type, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Failed to get D11 version: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    goto cfg_out;
  }
  cfg->d11inf.io_type = (uint8_t)io_type;
  brcmu_d11_attach(&cfg->d11inf);

  // NOTE: linux first verifies that 40 MHz operation is enabled in 2.4 GHz channels.
  err = brcmf_enable_bw40_2g(cfg);
  if (err == ZX_OK) {
    err = brcmf_fil_iovar_int_set(ifp, "obss_coex", BRCMF_OBSS_COEX_AUTO, nullptr);
  }

  drvr->config = cfg;
  err = brcmf_btcoex_attach(cfg);
  if (err != ZX_OK) {
    BRCMF_ERR("BT-coex initialisation failed (%d)", err);
    goto unreg_out;
  }

  if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_TDLS)) {
    err = brcmf_fil_iovar_int_set(ifp, "tdls_enable", 1, &fw_err);
    if (err != ZX_OK) {
      BRCMF_INFO("TDLS not enabled: %s, fw err %s", zx_status_get_string(err),
                 brcmf_fil_get_errstr(fw_err));
      goto btcoex_out;
    } else {
      brcmf_fweh_register(cfg->pub, BRCMF_E_TDLS_PEER_EVENT, brcmf_notify_tdls_peer_event);
    }
  }

  BRCMF_DBG(TEMP, "Exit");
  return ZX_OK;

btcoex_out:
  brcmf_btcoex_detach(cfg);
unreg_out:
  BRCMF_DBG(TEMP, "* * Would have called wiphy_unregister(cfg->wiphy);");
cfg_out:
  brcmf_deinit_cfg(cfg);
  brcmf_free_vif(vif);
  ifp->vif = nullptr;
cfg80211_info_out:
  free(cfg);
  return err;
}

void brcmf_cfg80211_detach(struct brcmf_cfg80211_info* cfg) {
  if (!cfg) {
    return;
  }

  brcmf_btcoex_detach(cfg);
  BRCMF_DBG(TEMP, "* * Would have called wiphy_unregister(cfg->wiphy);");
  brcmf_deinit_cfg(cfg);
  brcmf_clear_assoc_ies(cfg);
  free(cfg);
}

zx_status_t brcmf_clear_states(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_pub* drvr = cfg->pub;
  struct brcmf_cfg80211_vif* client_vif = drvr->iflist[0]->vif;
  struct net_device* client = client_vif->wdev.netdev;
  struct net_device* softap = cfg_to_softap_ndev(cfg);

  // Stop all interfaces.
  brcmf_if_stop(client);
  if (softap != nullptr)
    brcmf_if_stop(softap);

  // Stop all the timers(for all interfaces).
  cfg->disconnect_timer->Stop();
  cfg->signal_report_timer->Stop();
  cfg->ap_start_timer->Stop();
  cfg->connect_timer->Stop();

  // Clear all driver scan states.
  brcmf_clear_bit(brcmf_scan_status_bit_t::BUSY, &cfg->scan_status);
  brcmf_clear_bit(brcmf_scan_status_bit_t::SUPPRESS, &cfg->scan_status);

  // Clear connect and disconnect states for primary iface.
  brcmf_clear_bit(brcmf_vif_status_bit_t::SAE_AUTHENTICATING, &client_vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTING, &client_vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::CONNECTED, &client_vif->sme_state);
  brcmf_clear_bit(brcmf_vif_status_bit_t::DISCONNECTING, &client_vif->sme_state);

  // Always return ZX_OK.
  return ZX_OK;
}
