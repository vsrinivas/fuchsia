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

#include "core.h"

#include <endian.h>
#include <netinet/if_ether.h>
#include <pthread.h>
#include <threads.h>
#include <zircon/status.h>

#include <algorithm>
#include <atomic>

#include <wlan/common/phy.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "cfg80211.h"
#include "common.h"
#include "debug.h"
#include "feature.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "macros.h"
#include "netbuf.h"
#include "pno.h"
#include "proto.h"
#include "workqueue.h"

#define MAX_WAIT_FOR_8021X_TX_MSEC (950)

const char* brcmf_ifname(struct brcmf_if* ifp) {
  if (!ifp) {
    return "<if_null>";
  }

  if (ifp->ndev) {
    return ifp->ndev->name;
  }

  return "<if_none>";
}

struct brcmf_if* brcmf_get_ifp(struct brcmf_pub* drvr, int ifidx) {
  struct brcmf_if* ifp;
  int32_t bsscfgidx;

  if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
    BRCMF_ERR("ifidx %d out of range", ifidx);
    return NULL;
  }

  ifp = NULL;
  bsscfgidx = drvr->if2bss[ifidx];
  if (bsscfgidx >= 0) {
    ifp = drvr->iflist[bsscfgidx];
  }

  return ifp;
}

void brcmf_configure_arp_nd_offload(struct brcmf_if* ifp, bool enable) {
  zx_status_t err;
  uint32_t mode;
  bcme_status_t fw_err = BCME_OK;

  if (enable) {
    mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
  } else {
    mode = 0;
  }

  /* Try to set and enable ARP offload feature, this may fail, then it  */
  /* is simply not supported and err 0 will be returned                 */
  err = brcmf_fil_iovar_int_set(ifp, "arp_ol", mode, &fw_err);
  if (err != ZX_OK) {
    BRCMF_DBG(TRACE, "failed to set ARP offload mode to 0x%x, err=%s, fw_err=%s", mode,
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
  } else {
    err = brcmf_fil_iovar_int_set(ifp, "arpoe", enable, &fw_err);
    if (err != ZX_OK) {
      BRCMF_DBG(TRACE, "failed to configure (%d) ARP offload err=%s, fw_err=%s", enable,
                zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    } else {
      BRCMF_DBG(TRACE, "successfully configured (%d) ARP offload to 0x%x", enable, mode);
    }
  }

  err = brcmf_fil_iovar_int_set(ifp, "ndoe", enable, &fw_err);
  if (err != ZX_OK) {
    BRCMF_DBG(TRACE, "failed to configure (%d) ND offload err=%s, fw_err=%s", enable,
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
  } else {
    BRCMF_DBG(TRACE, "successfully configured (%d) ND offload to 0x%x", enable, mode);
  }
}

static void brcmf_set_multicast_list(struct brcmf_if* ifp) {
  struct net_device* ndev;
  struct netdev_hw_addr* ha;
  uint32_t cmd_value, cnt;
  uint32_t cnt_le;
  char* buf;
  char* bufp;
  uint32_t buflen;
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d", ifp->bsscfgidx);

  ndev = ifp->ndev;

  /* Determine initial value of allmulti flag */
  cmd_value = ndev->multicast_promisc;

  /* Send down the multicast list first. */
  cnt = netdev_mc_count(ndev);
  buflen = sizeof(cnt) + (cnt * ETH_ALEN);
  buf = static_cast<decltype(buf)>(malloc(buflen));
  if (!buf) {
    return;
  }
  bufp = buf;

  cnt_le = cnt;
  memcpy(bufp, &cnt_le, sizeof(cnt_le));
  bufp += sizeof(cnt_le);

  netdev_for_each_mc_addr(ha, ndev) {
    if (!cnt) {
      break;
    }
    memcpy(bufp, ha->addr, ETH_ALEN);
    bufp += ETH_ALEN;
    cnt--;
  }

  err = brcmf_fil_iovar_data_set(ifp, "mcast_list", buf, buflen, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting mcast_list failed: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
    cmd_value = cnt ? true : cmd_value;
  }

  free(buf);

  /*
   * Now send the allmulti setting.  This is based on the setting in the
   * net_device flags, but might be modified above to be turned on if we
   * were trying to set some addresses and dongle rejected it...
   */
  err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting allmulti failed: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  /* Promiscuous mode is currently unsupported */
  cmd_value = false;
  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting BRCMF_C_SET_PROMISC failed, %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  /* In general, the configuration of ARP offloading is interface-dependent (enabled for client
   * and disabled for AP). The code below is intended to override the default setting in the
   * specific case where promiscuous mode is enabled. In that case, we want to disable ARP
   * offloading so those packets are sent to the interface. See issue 52305 for context.  We could
   * remove these lines of code since promiscuous mode is currently unsupported, but we should
   * probably leave them in so the problem doesn't pop up again if/when support is added.
   */
  if (cmd_value) {
    brcmf_configure_arp_nd_offload(ifp, false);
  }
}

static void brcmf_set_multicast_list_worker(WorkItem* work) {
  struct brcmf_if* ifp;
  ifp = containerof(work, struct brcmf_if, multicast_work);
  brcmf_set_multicast_list(ifp);
}

zx_status_t brcmf_netdev_set_mac_address(struct net_device* ndev, uint8_t* addr) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  zx_status_t err;
  bcme_status_t fw_err = BCME_OK;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d", ifp->bsscfgidx);

  err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", addr, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting cur_etheraddr failed: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  } else {
    BRCMF_DBG(TRACE, "updated to %pM", addr);
    memcpy(ifp->mac_addr, addr, ETH_ALEN);
    memcpy(ifp->ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
  }
  return err;
}

void brcmf_netdev_set_multicast_list(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  if (brcmf_bus_get_bus_type(ifp->drvr->bus_if) == BRCMF_BUS_TYPE_SIM) {
    brcmf_set_multicast_list(ifp);
  } else {
    WorkQueue::ScheduleDefault(&ifp->multicast_work);
  }
}

void brcmf_netdev_set_allmulti(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  uint32_t cmd_value = ndev->multicast_promisc;
  zx_status_t err = ZX_OK;
  bcme_status_t fw_err = BCME_OK;

  err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting allmulti failed: %s, fw err %s", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
}

void brcmf_netdev_start_xmit(struct net_device* ndev,
                             std::unique_ptr<wlan::brcmfmac::Netbuf> netbuf) {
  zx_status_t ret;
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_pub* drvr = ifp->drvr;
  struct ethhdr eh;
  const size_t netbuf_size = netbuf->size();

  BRCMF_DBG(DATA, "Enter, bsscfgidx=%d", ifp->bsscfgidx);

  /* Can the device send data? */
  if (drvr->bus_if->state != BRCMF_BUS_UP) {
    BRCMF_ERR("xmit rejected state=%d", drvr->bus_if->state);
    netif_stop_queue(ndev);
    ret = ZX_ERR_UNAVAILABLE;
    goto done;
  }

  /* validate length for ether packet */
  if (netbuf->size() < sizeof(eh)) {
    ret = ZX_ERR_INVALID_ARGS;
    netbuf->Return(ret);
    goto done;
  }
  eh = *(struct ethhdr*)(netbuf->data());

  if (eh.h_proto == htobe16(ETH_P_PAE)) {
    ifp->pend_8021x_cnt.fetch_add(1);
  }

  netbuf->SetPriority(brcmf_cfg80211_classify8021d((uint8_t*)netbuf->data(), netbuf->size()));

  ret = brcmf_proto_tx_queue_data(drvr, ifp->ifidx, std::move(netbuf));
  if (ret != ZX_OK) {
    brcmf_txfinalize(ifp, &eh, false);
  }

done:
  if (ret != ZX_OK) {
    ndev->stats.tx_dropped++;
  } else {
    ndev->stats.tx_packets++;
    ndev->stats.tx_bytes += netbuf_size;
  }
  /* No status to return: we always eat the packet */
}

void brcmf_txflowblock_if(struct brcmf_if* ifp, enum brcmf_netif_stop_reason reason, bool state) {
  if (!ifp || !ifp->ndev) {
    return;
  }

  BRCMF_DBG(TRACE, "enter: bsscfgidx=%d stop=0x%X reason=%d state=%d", ifp->bsscfgidx,
            ifp->netif_stop, reason, state);

  // spin_lock_irqsave(&ifp->netif_stop_lock, flags);
  ifp->drvr->irq_callback_lock.lock();

  if (state) {
    if (!ifp->netif_stop) {
      netif_stop_queue(ifp->ndev);
    }
    ifp->netif_stop |= reason;
  } else {
    ifp->netif_stop &= ~reason;
    if (!ifp->netif_stop) {
      brcmf_enable_tx(ifp->ndev);
    }
  }
  // spin_unlock_irqrestore(&ifp->netif_stop_lock, flags);
  ifp->drvr->irq_callback_lock.unlock();
}

void brcmf_netif_rx(struct brcmf_if* ifp, const void* data, size_t size) {
  const ethhdr* const eh = reinterpret_cast<const ethhdr*>(data);
  if (address_is_multicast(eh->h_dest) && !address_is_broadcast(eh->h_dest)) {
    ifp->ndev->stats.multicast++;
  }

  if (!(ifp->ndev->is_up)) {
    return;
  }

  ifp->ndev->stats.rx_bytes += size;
  ifp->ndev->stats.rx_packets++;

  BRCMF_DBG(DATA, "rx proto=0x%X len %zu", be16toh(eh->h_proto), size);
  brcmf_cfg80211_rx(ifp, data, size);
}

static zx_status_t brcmf_rx_hdrpull(struct brcmf_pub* drvr, struct brcmf_netbuf* netbuf,
                                    struct brcmf_if** ifp) {
  zx_status_t ret;

  /* process and remove protocol-specific header */
  ret = brcmf_proto_hdrpull(drvr, true, netbuf, ifp);

  if (ret != ZX_OK || !(*ifp) || !(*ifp)->ndev) {
    if (ret != ZX_ERR_BUFFER_TOO_SMALL && *ifp) {
      (*ifp)->ndev->stats.rx_errors++;
    }
    brcmu_pkt_buf_free_netbuf(netbuf);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

void brcmf_rx_frame(brcmf_pub* drvr, brcmf_netbuf* netbuf, bool handle_event) {
  struct brcmf_if* ifp;

  BRCMF_DBG(DATA, "Enter: %s: rxp=%p", device_get_name(drvr->zxdev), netbuf);

  if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
    BRCMF_DBG(TEMP, "hdrpull returned nonzero");
    return;
  }

  if (brcmf_proto_is_reorder_netbuf(netbuf)) {
    brcmf_proto_rxreorder(ifp, netbuf);
  } else {
    /* Process special event packets */
    if (handle_event) {
      brcmf_fweh_process_event(ifp->drvr, reinterpret_cast<brcmf_event*>(netbuf->data),
                               netbuf->len);
    }

    brcmf_netif_rx(ifp, netbuf->data, netbuf->len);
    brcmu_pkt_buf_free_netbuf(netbuf);
  }
}

void brcmf_rx_event(brcmf_pub* drvr, brcmf_netbuf* netbuf) {
  struct brcmf_if* ifp;

  BRCMF_DBG(EVENT, "Enter: %s: rxp=%p", device_get_name(drvr->zxdev), netbuf);

  if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
    return;
  }

  brcmf_fweh_process_event(ifp->drvr, reinterpret_cast<brcmf_event*>(netbuf->data), netbuf->len);
  brcmu_pkt_buf_free_netbuf(netbuf);
}

void brcmf_txfinalize(struct brcmf_if* ifp, const struct ethhdr* eh, bool success) {
  const uint16_t type = be16toh(eh->h_proto);
  if (type == ETH_P_PAE) {
    if (ifp->pend_8021x_cnt.fetch_sub(1) == 1) {
      sync_completion_signal(&ifp->pend_8021x_wait);
    }
  }

  if (!success) {
    ifp->ndev->stats.tx_errors++;
  }
}

static zx_status_t brcmf_netdev_stop(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d", ifp->bsscfgidx);

  brcmf_cfg80211_down(ndev);

  brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear", NULL, 0, nullptr);

  brcmf_net_setcarrier(ifp, false);

  return ZX_OK;
}

zx_status_t brcmf_netdev_open(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_pub* drvr = ifp->drvr;
  struct brcmf_bus* bus_if = drvr->bus_if;
  uint32_t toe_ol;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d", ifp->bsscfgidx);

  /* If bus is not ready, can't continue */
  if (bus_if->state != BRCMF_BUS_UP) {
    BRCMF_ERR("failed bus is not ready");
    return ZX_ERR_UNAVAILABLE;
  }

  ifp->pend_8021x_cnt.store(0);

  /* Get current TOE mode from dongle */
  if (brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_ol, nullptr) == ZX_OK &&
      (toe_ol & TOE_TX_CSUM_OL) != 0) {
    ndev->features |= NETIF_F_IP_CSUM;
  } else {
    ndev->features &= ~NETIF_F_IP_CSUM;
  }

  if (brcmf_cfg80211_up(ndev) != ZX_OK) {
    BRCMF_ERR("failed to bring up cfg80211");
    return ZX_ERR_IO;
  }

  /* Clear, carrier, set when connected or AP mode. */
  BRCMF_DBG(TEMP, "* * Would have called netif_carrier_off(ndev);");
  return ZX_OK;
}

zx_status_t brcmf_net_attach(struct brcmf_if* ifp, bool rtnl_locked) {
  struct brcmf_pub* drvr = ifp->drvr;
  struct net_device* ndev = ifp->ndev;
  BRCMF_DBG(TRACE, "Enter-New, bsscfgidx=%d mac=%pM", ifp->bsscfgidx, ifp->mac_addr);

  ndev->needed_headroom += drvr->hdrlen;
  ifp->multicast_work = WorkItem(brcmf_set_multicast_list_worker);
  return ZX_OK;
}

static void brcmf_net_detach(struct net_device* ndev, bool rtnl_locked) {
  // TODO(cphoenix): Make sure devices are removed and memory is freed properly. This code
  // is probably wrong. See fxbug.dev/29675.
  brcmf_free_net_device_vif(ndev);
  brcmf_free_net_device(ndev);
}

void brcmf_net_setcarrier(struct brcmf_if* ifp, bool on) {
  struct net_device* ndev;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d carrier=%d", ifp->bsscfgidx, on);

  ndev = ifp->ndev;
  brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_DISCONNECTED, !on);
  if (on) {
    if (!netif_carrier_ok(ndev)) {
      netif_carrier_on(ndev);
    }

  } else {
    if (netif_carrier_ok(ndev)) {
      netif_carrier_off(ndev);
    }
  }
}

zx_status_t brcmf_add_if(struct brcmf_pub* drvr, int32_t bsscfgidx, int32_t ifidx, const char* name,
                         uint8_t* mac_addr, struct brcmf_if** if_out) {
  struct brcmf_if* ifp;
  struct net_device* ndev;

  if (if_out) {
    *if_out = NULL;
  }

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d, name=%s", bsscfgidx, ifidx, name);
  ifp = drvr->iflist[bsscfgidx];

  /*
   * Never touch ifidx 0. This is the primary network interface and should never be
   * modified by this function after its first call.
   */
  if (ifp && ifidx == 0) {
    BRCMF_DBG(INFO,
              "netdev:%s ignoring IF event requesting to modify the primary network interface",
              ifp->ndev->name);
    return ZX_ERR_INVALID_ARGS;
  }

  /*
   * Delete the existing interface before overwriting it
   * in case we missed the BRCMF_E_IF_DEL event.
   */
  if (ifp) {
    BRCMF_ERR("Iface at ifidx %d already exists. Replacing the existing netdev:%s with netdev:%s.",
              ifidx, ifp->ndev->name, name);
    netif_stop_queue(ifp->ndev);
    brcmf_net_detach(ifp->ndev, false);
    drvr->iflist[bsscfgidx] = NULL;
  }

  /* Allocate netdev, including space for private structure */
  ndev = brcmf_allocate_net_device(sizeof(*ifp), name);
  if (ndev == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  ndev->needs_free_net_device = true;
  ifp = ndev_to_if(ndev);
  ifp->ndev = ndev;
  /* store mapping ifidx to bsscfgidx */
  if (drvr->if2bss[ifidx] == BRCMF_BSSIDX_INVALID) {
    drvr->if2bss[ifidx] = bsscfgidx;
  }

  ifp->drvr = drvr;
  drvr->iflist[bsscfgidx] = ifp;
  ifp->ifidx = ifidx;
  ifp->bsscfgidx = bsscfgidx;

  ifp->pend_8021x_wait = {};
  // spin_lock_init(&ifp->netif_stop_lock);

  if (mac_addr != nullptr) {
    memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);
  }
  BRCMF_INFO("Created a new iface. netdev:%s, bsscfgidx: %d, ifidx: %d", ndev->name, bsscfgidx,
             ifidx);
  if (if_out) {
    *if_out = ifp;
  }
  // This is probably unnecessary - just test/verify after taking it out please!
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  BRCMF_DBG(TRACE, "Exit");
  return ZX_OK;
}

static void brcmf_del_if(struct brcmf_pub* drvr, int32_t bsscfgidx, bool rtnl_locked) {
  struct brcmf_if* ifp;

  ifp = drvr->iflist[bsscfgidx];
  drvr->iflist[bsscfgidx] = NULL;
  if (!ifp) {
    BRCMF_ERR("Null interface, bsscfgidx=%d", bsscfgidx);
    return;
  }
  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d", bsscfgidx, ifp->ifidx);
  if (drvr->if2bss[ifp->ifidx] == bsscfgidx) {
    drvr->if2bss[ifp->ifidx] = BRCMF_BSSIDX_INVALID;
  }
  if (ifp->ndev) {
    if (bsscfgidx == 0) {
      if (ifp->ndev->initialized_for_ap) {
        rtnl_lock();
        brcmf_netdev_stop(ifp->ndev);
        rtnl_unlock();
      }
    } else {
      netif_stop_queue(ifp->ndev);
    }

    if (ifp->ndev->initialized_for_ap) {
      ifp->multicast_work.Cancel();
    }
    brcmf_net_detach(ifp->ndev, rtnl_locked);
  }
}

void brcmf_remove_interface(struct brcmf_if* ifp, bool rtnl_locked) {
  if (!ifp || WARN_ON(ifp->drvr->iflist[ifp->bsscfgidx] != ifp)) {
    return;
  }
  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d", ifp->bsscfgidx, ifp->ifidx);
  brcmf_proto_del_if(ifp->drvr, ifp);
  brcmf_del_if(ifp->drvr, ifp->bsscfgidx, rtnl_locked);
}

zx_status_t brcmf_attach(brcmf_pub* drvr) {
  BRCMF_DBG(TRACE, "Enter");

  if (!drvr->bus_if || !drvr->settings) {
    return ZX_ERR_BAD_STATE;
  }

  /* attach firmware event handler */
  brcmf_fweh_attach(drvr);
  return ZX_OK;
}

zx_status_t brcmf_bus_started(brcmf_pub* drvr) {
  zx_status_t ret = ZX_ERR_IO;
  struct brcmf_bus* bus_if = drvr->bus_if;
  struct brcmf_if* ifp;

  BRCMF_DBG(TRACE, "Enter");

  /* add primary networking interface */
  // TODO(fxbug.dev/29361): Name uniqueness
  ret = brcmf_add_if(drvr, 0, 0, kPrimaryNetworkInterfaceName, NULL, &ifp);
  if (ret != ZX_OK) {
    return ret;
  }

  /* signal bus ready */
  brcmf_bus_change_state(bus_if, BRCMF_BUS_UP);
  /* Bus is ready, do any initialization */
  ret = brcmf_c_preinit_dcmds(ifp);
  if (ret != ZX_OK) {
    goto fail;
  }

  /* assure we have chipid before feature attach */
  if (!bus_if->chip) {
    bus_if->chip = drvr->revinfo.fwrevinfo.chipnum;
    bus_if->chiprev = drvr->revinfo.fwrevinfo.chiprev;
    BRCMF_DBG(INFO, "firmware revinfo: chip %x (%d) rev %d", bus_if->chip, bus_if->chip,
              bus_if->chiprev);
  }
  brcmf_feat_attach(drvr);

  ret = brcmf_proto_init_done(drvr);
  if (ret != ZX_OK) {
    goto fail;
  }

  brcmf_proto_add_if(drvr, ifp);

  ret = brcmf_cfg80211_attach(drvr);
  if (ret != ZX_OK) {
    BRCMF_ERR("brcmf_cfg80211_attach failed (%d).", ret);
    goto fail;
  }

  ret = brcmf_fweh_activate_events(ifp);
  if (ret != ZX_OK) {
    BRCMF_ERR("FWEH activation failed (%d)", ret);
    goto fail;
  }

  ret = brcmf_net_attach(ifp, false);

  if (ret != ZX_OK) {
    goto fail;
  }

  return ZX_OK;

fail:
  BRCMF_ERR("brcmf_bus started failed: (%d)", ret);
  if (drvr->config) {
    brcmf_cfg80211_detach(drvr->config);
    drvr->config = NULL;
  }
  brcmf_net_detach(ifp->ndev, false);

  drvr->iflist[0] = NULL;
  drvr->iflist[1] = NULL;
  if (drvr->settings->ignore_probe_fail) {
    ret = ZX_OK;
  }

  return ret;
}

void brcmf_bus_add_txhdrlen(brcmf_pub* drvr, uint len) {
  if (drvr) {
    drvr->hdrlen += len;
  }
}

void brcmf_dev_reset(brcmf_pub* drvr) {
  if (drvr == NULL) {
    return;
  }

  if (drvr->iflist[0]) {
    brcmf_fil_cmd_int_set(drvr->iflist[0], BRCMF_C_TERMINATED, 1, nullptr);
  }
}

void brcmf_detach(brcmf_pub* drvr) {
  int32_t i;
  BRCMF_DBG(TRACE, "Enter");

  if (drvr == NULL) {
    return;
  }

  /* stop firmware event handling */
  brcmf_fweh_detach(drvr);

  brcmf_bus_change_state(drvr->bus_if, BRCMF_BUS_DOWN);

  /* make sure primary interface removed last */
  for (i = BRCMF_MAX_IFS - 1; i > -1; i--) {
    brcmf_remove_interface(drvr->iflist[i], false);
  }

  brcmf_cfg80211_detach(drvr->config);

  brcmf_bus_stop(drvr->bus_if);
}

zx_status_t brcmf_iovar_data_set(brcmf_pub* drvr, const char* name, void* data, uint32_t len,
                                 bcme_status_t* fwerr_ptr) {
  struct brcmf_if* ifp = drvr->iflist[0];

  return brcmf_fil_iovar_data_set(ifp, name, data, len, fwerr_ptr);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if* ifp) { return ifp->pend_8021x_cnt.load(); }

void brcmf_write_net_device_name(struct net_device* dev, const char* name) {
  strlcpy(dev->name, name, sizeof(dev->name));
  if (strlen(name) + 1 > sizeof(dev->name)) {
    BRCMF_WARN("Truncated netdev:%s to netdev:%s", name, dev->name);
  }
}

struct net_device* brcmf_allocate_net_device(size_t priv_size, const char* name) {
  struct net_device* dev = static_cast<decltype(dev)>(calloc(1, sizeof(*dev)));
  if (dev == NULL) {
    return NULL;
  }
  dev->priv = static_cast<decltype(dev->priv)>(calloc(1, priv_size));
  if (dev->priv == NULL) {
    free(dev);
    return NULL;
  }
  brcmf_write_net_device_name(dev, name);
  return dev;
}

void brcmf_free_net_device(struct net_device* dev) {
  if (dev != NULL) {
    free(dev->priv);
    free(dev);
  }
}

void brcmf_enable_tx(struct net_device* dev) {
  BRCMF_DBG(INFO, " * * NOTE: brcmf_enable_tx called. Enable TX. (Was netif_wake_queue)");
}
void brcmf_netdev_wait_pend8021x(struct brcmf_if* ifp) {
  zx_status_t result;

  sync_completion_reset(&ifp->pend_8021x_wait);
  if (!brcmf_get_pend_8021x_cnt(ifp)) {
    return;
  }
  result = sync_completion_wait(&ifp->pend_8021x_wait, ZX_MSEC(MAX_WAIT_FOR_8021X_TX_MSEC));

  if (result != ZX_OK) {
    BRCMF_ERR("Timed out waiting for no pending 802.1x packets");
  }
}

void brcmf_bus_change_state(struct brcmf_bus* bus, enum brcmf_bus_state state) {
  BRCMF_DBG(TRACE, "%d -> %d", bus->state, state);
  bus->state = state;

#if 0
  struct brcmf_pub* drvr = bus->priv.sdio->drvr.get();
  struct net_device* ndev;
  int ifidx;

  if (state == BRCMF_BUS_UP) {
    for (ifidx = 0; ifidx < BRCMF_MAX_IFS; ifidx++) {
      if ((drvr->iflist[ifidx]) && (drvr->iflist[ifidx]->ndev)) {
        ndev = drvr->iflist[ifidx]->ndev;
        // TODO(cphoenix): Implement Fuchsia equivalent of...
        // BRCMF_DBG(INFO, "This code called netif_wake_queue(ndev)");
        // BRCMF_DBG(INFO, "  if netif_queue_stopped(ndev). Do the Fuchsia equivalent.");
      }
    }
  }
#endif
}
