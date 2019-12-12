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
    BRCMF_ERR("ifidx %d out of range\n", ifidx);
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
  int32_t fw_err = 0;

  if (enable) {
    mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
  } else {
    mode = 0;
  }

  /* Try to set and enable ARP offload feature, this may fail, then it  */
  /* is simply not supported and err 0 will be returned                 */
  err = brcmf_fil_iovar_int_set(ifp, "arp_ol", mode, &fw_err);
  if (err != ZX_OK) {
    BRCMF_DBG(TRACE, "failed to set ARP offload mode to 0x%x, err=%s, fw_err=%s\n", mode,
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
  } else {
    err = brcmf_fil_iovar_int_set(ifp, "arpoe", enable, &fw_err);
    if (err != ZX_OK) {
      BRCMF_DBG(TRACE, "failed to configure (%d) ARP offload err=%s, fw_err=%s\n", enable,
                zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
    } else {
      BRCMF_DBG(TRACE, "successfully configured (%d) ARP offload to 0x%x\n", enable, mode);
    }
  }

  err = brcmf_fil_iovar_int_set(ifp, "ndoe", enable, &fw_err);
  if (err != ZX_OK) {
    BRCMF_DBG(TRACE, "failed to configure (%d) ND offload err=%s, fw_err=%s\n", enable,
              zx_status_get_string(err), brcmf_fil_get_errstr(fw_err));
  } else {
    BRCMF_DBG(TRACE, "successfully configured (%d) ND offload to 0x%x\n", enable, mode);
  }
}

static void _brcmf_set_multicast_list(WorkItem* work) {
  struct brcmf_if* ifp;
  struct net_device* ndev;
  struct netdev_hw_addr* ha;
  uint32_t cmd_value, cnt;
  uint32_t cnt_le;
  char* buf;
  char* bufp;
  uint32_t buflen;
  zx_status_t err;
  int32_t fw_err = 0;

  ifp = containerof(work, struct brcmf_if, multicast_work);

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

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
    BRCMF_ERR("Setting mcast_list failed: %s, fw err %s\n", zx_status_get_string(err),
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
    BRCMF_ERR("Setting allmulti failed: %s, fw err %s\n", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }

  /*Finally, pick up the PROMISC flag */
  cmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
  err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting BRCMF_C_SET_PROMISC failed, %s, fw err %s\n", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  }
  brcmf_configure_arp_nd_offload(ifp, !cmd_value);
}

zx_status_t brcmf_netdev_set_mac_address(struct net_device* ndev, uint8_t* addr) {
  struct brcmf_if* ifp = ndev_to_if(ndev);
  zx_status_t err;
  int32_t fw_err = 0;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

  err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", addr, ETH_ALEN, &fw_err);
  if (err != ZX_OK) {
    BRCMF_ERR("Setting cur_etheraddr failed: %s, fw err %s\n", zx_status_get_string(err),
              brcmf_fil_get_errstr(fw_err));
  } else {
    BRCMF_DBG(TRACE, "updated to %pM\n", addr);
    memcpy(ifp->mac_addr, addr, ETH_ALEN);
    memcpy(ifp->ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
  }
  return err;
}

void brcmf_netdev_set_multicast_list(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  WorkQueue::ScheduleDefault(&ifp->multicast_work);
}

void brcmf_netdev_start_xmit(struct net_device* ndev, ethernet_netbuf_t* ethernet_netbuf) {
  zx_status_t ret;
  struct brcmf_if* ifp = ndev_to_if(ndev);
  struct brcmf_pub* drvr = ifp->drvr;
  struct brcmf_netbuf* netbuf = nullptr;
  struct ethhdr* eh = nullptr;
  int head_delta;

  BRCMF_DBG(DATA, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

  /* Can the device send data? */
  if (drvr->bus_if->state != BRCMF_BUS_UP) {
    BRCMF_ERR("xmit rejected state=%d\n", drvr->bus_if->state);
    netif_stop_queue(ndev);
    ret = ZX_ERR_UNAVAILABLE;
    goto done;
  }

  netbuf = brcmf_netbuf_allocate(ethernet_netbuf->data_size + drvr->hdrlen);
  brcmf_netbuf_grow_tail(netbuf, ethernet_netbuf->data_size + drvr->hdrlen);
  brcmf_netbuf_shrink_head(netbuf, drvr->hdrlen);
  memcpy(netbuf->data, ethernet_netbuf->data_buffer, ethernet_netbuf->data_size);

  /* Make sure there's enough writeable headroom */
  if (brcmf_netbuf_head_space(netbuf) < drvr->hdrlen) {
    head_delta = std::max<int>(drvr->hdrlen - brcmf_netbuf_head_space(netbuf), 0);

    BRCMF_DBG(INFO, "%s: insufficient headroom (%d)\n", brcmf_ifname(ifp), head_delta);
    drvr->bus_if->stats.pktcowed.fetch_add(1);
    ret = brcmf_netbuf_grow_realloc(netbuf, ALIGN(head_delta, NET_NETBUF_PAD), 0);
    if (ret != ZX_OK) {
      BRCMF_ERR("%s: failed to expand headroom\n", brcmf_ifname(ifp));
      drvr->bus_if->stats.pktcow_failed.fetch_add(1);
      // TODO(cphoenix): Shouldn't I brcmf_netbuf_free here?
      goto done;
    }
  }

  /* validate length for ether packet */
  if (netbuf->len < sizeof(*eh)) {
    ret = ZX_ERR_INVALID_ARGS;
    brcmf_netbuf_free(netbuf);
    goto done;
  }

  eh = (struct ethhdr*)(netbuf->data);

  if (eh->h_proto == htobe16(ETH_P_PAE)) {
    ifp->pend_8021x_cnt.fetch_add(1);
  }

  /* determine the priority */
  if ((netbuf->priority == 0) || (netbuf->priority > 7)) {
    netbuf->priority = cfg80211_classify8021d(netbuf, NULL);
  }

  ret = brcmf_proto_tx_queue_data(drvr, ifp->ifidx, netbuf);
  if (ret != ZX_OK) {
    brcmf_txfinalize(ifp, netbuf, false);
  }

done:
  if (ret != ZX_OK) {
    ndev->stats.tx_dropped++;
  } else {
    ndev->stats.tx_packets++;
    ndev->stats.tx_bytes += netbuf->len;
  }
  /* No status to return: we always eat the packet */
}

void brcmf_txflowblock_if(struct brcmf_if* ifp, enum brcmf_netif_stop_reason reason, bool state) {
  if (!ifp || !ifp->ndev) {
    return;
  }

  BRCMF_DBG(TRACE, "enter: bsscfgidx=%d stop=0x%X reason=%d state=%d\n", ifp->bsscfgidx,
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

void brcmf_netif_rx(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {
  const ethhdr* const eh = reinterpret_cast<const ethhdr*>(netbuf->data);
  if (address_is_multicast(eh->h_dest) && !address_is_broadcast(eh->h_dest)) {
    ifp->ndev->stats.multicast++;
  }

  if (!(ifp->ndev->flags & IFF_UP)) {
    brcmu_pkt_buf_free_netbuf(netbuf);
    return;
  }

  ifp->ndev->stats.rx_bytes += netbuf->len;
  ifp->ndev->stats.rx_packets++;

  BRCMF_DBG(DATA, "rx proto=0x%X len %d\n", be16toh(eh->h_proto), netbuf->len);
  brcmf_cfg80211_rx(ifp, netbuf);
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

  BRCMF_DBG(DATA, "Enter: %s: rxp=%p\n", device_get_name(drvr->zxdev), netbuf);

  if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
    BRCMF_DBG(TEMP, "hdrpull returned nonzero\n");
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

    brcmf_netif_rx(ifp, netbuf);
  }
}

void brcmf_rx_event(brcmf_pub* drvr, brcmf_netbuf* netbuf) {
  struct brcmf_if* ifp;

  BRCMF_DBG(EVENT, "Enter: %s: rxp=%p\n", device_get_name(drvr->zxdev), netbuf);

  if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
    return;
  }

  brcmf_fweh_process_event(ifp->drvr, reinterpret_cast<brcmf_event*>(netbuf->data), netbuf->len);
  brcmu_pkt_buf_free_netbuf(netbuf);
}

void brcmf_txfinalize(struct brcmf_if* ifp, struct brcmf_netbuf* txp, bool success) {
  struct ethhdr* eh;
  uint16_t type;

  eh = (struct ethhdr*)(txp->data);
  type = be16toh(eh->h_proto);

  if (type == ETH_P_PAE) {
    if (ifp->pend_8021x_cnt.fetch_sub(1) == 1) {
      sync_completion_signal(&ifp->pend_8021x_wait);
    }
  }

  if (!success) {
    ifp->ndev->stats.tx_errors++;
  }

  brcmu_pkt_buf_free_netbuf(txp);
}

static zx_status_t brcmf_netdev_stop(struct net_device* ndev) {
  struct brcmf_if* ifp = ndev_to_if(ndev);

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

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

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

  /* If bus is not ready, can't continue */
  if (bus_if->state != BRCMF_BUS_UP) {
    BRCMF_ERR("failed bus is not ready\n");
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
    BRCMF_ERR("failed to bring up cfg80211\n");
    return ZX_ERR_IO;
  }

  /* Clear, carrier, set when connected or AP mode. */
  BRCMF_DBG(TEMP, "* * Would have called netif_carrier_off(ndev);\n");
  return ZX_OK;
}

zx_status_t brcmf_net_attach(struct brcmf_if* ifp, bool rtnl_locked) {
  struct brcmf_pub* drvr = ifp->drvr;
  struct net_device* ndev = ifp->ndev;
  BRCMF_DBG(TRACE, "Enter-New, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx, ifp->mac_addr);

  ndev->needed_headroom += drvr->hdrlen;
  ifp->multicast_work = WorkItem(_brcmf_set_multicast_list);
  return ZX_OK;
}

static void brcmf_net_detach(struct net_device* ndev, bool rtnl_locked) {
  // TODO(cphoenix): Make sure devices are removed and memory is freed properly. This code
  // is probably wrong. See WLAN-1057.
  brcmf_free_net_device_vif(ndev);
  brcmf_free_net_device(ndev);
}

void brcmf_net_setcarrier(struct brcmf_if* ifp, bool on) {
  struct net_device* ndev;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d carrier=%d\n", ifp->bsscfgidx, on);

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

zx_status_t brcmf_net_p2p_open(struct net_device* ndev) {
  BRCMF_DBG(TRACE, "Enter\n");

  return brcmf_cfg80211_up(ndev);
}

zx_status_t brcmf_net_p2p_stop(struct net_device* ndev) {
  BRCMF_DBG(TRACE, "Enter\n");

  return brcmf_cfg80211_down(ndev);
}

void brcmf_net_p2p_start_xmit(struct brcmf_netbuf* netbuf, struct net_device* ndev) {
  if (netbuf) {
    brcmf_netbuf_free(netbuf);
  }
}

static zx_status_t brcmf_net_p2p_attach(struct brcmf_if* ifp) {
  struct net_device* ndev;

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx, ifp->mac_addr);
  ndev = ifp->ndev;

  ndev->initialized_for_ap = false;

  /* set the mac address */
  memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

  BRCMF_ERR("* * Tried to register_netdev(ndev); do the ZX thing instead.");
  // TODO(cphoenix): Add back the appropriate "fail:" code
  // If register_netdev failed, goto fail;

  BRCMF_DBG(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

  return ZX_OK;

  // fail:
  //    ifp->drvr->iflist[ifp->bsscfgidx] = NULL;
  //    ndev->netdev_ops = NULL;
  //    return ZX_ERR_IO_NOT_PRESENT;
}

zx_status_t brcmf_add_if(struct brcmf_pub* drvr, int32_t bsscfgidx, int32_t ifidx, bool is_p2pdev,
                         const char* name, uint8_t* mac_addr, struct brcmf_if** if_out) {
  struct brcmf_if* ifp;
  struct net_device* ndev;

  if (if_out) {
    *if_out = NULL;
  }

  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifidx);

  ifp = drvr->iflist[bsscfgidx];
  /*
   * Delete the existing interface before overwriting it
   * in case we missed the BRCMF_E_IF_DEL event.
   */
  if (ifp) {
    if (ifidx) {
      BRCMF_ERR("ERROR: netdev:%s already exists\n", ifp->ndev->name);
      netif_stop_queue(ifp->ndev);
      brcmf_net_detach(ifp->ndev, false);
      drvr->iflist[bsscfgidx] = NULL;
    } else {
      BRCMF_DBG(INFO, "netdev:%s ignore IF event\n", ifp->ndev->name);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (!drvr->settings->p2p_enable && is_p2pdev) {
    /* this is P2P_DEVICE interface */
    BRCMF_DBG(INFO, "allocate non-netdev interface\n");
    ifp = static_cast<decltype(ifp)>(calloc(1, sizeof(*ifp)));
    if (!ifp) {
      return ZX_ERR_NO_MEMORY;
    }
  } else {
    /* Allocate netdev, including space for private structure */
    ndev = brcmf_allocate_net_device(sizeof(*ifp), is_p2pdev ? "p2p" : name);
    if (!ndev) {
      return ZX_ERR_NO_MEMORY;
    }

    ndev->needs_free_net_device = true;
    ifp = ndev_to_if(ndev);
    ifp->ndev = ndev;
    /* store mapping ifidx to bsscfgidx */
    if (drvr->if2bss[ifidx] == BRCMF_BSSIDX_INVALID) {
      drvr->if2bss[ifidx] = bsscfgidx;
    }
  }

  ifp->drvr = drvr;
  drvr->iflist[bsscfgidx] = ifp;
  ifp->ifidx = ifidx;
  ifp->bsscfgidx = bsscfgidx;

  ifp->pend_8021x_wait = {};
  // spin_lock_init(&ifp->netif_stop_lock);

  if (mac_addr != NULL) {
    memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);
  }
  BRCMF_DBG(TRACE, " ==== if:%s (%pM) created ===\n", name, ifp->mac_addr);
  if (if_out) {
    *if_out = ifp;
  }
  // This is probably unnecessary - just test/verify after taking it out please!
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
  BRCMF_DBG(TRACE, "Exit\n");
  return ZX_OK;
}

static void brcmf_del_if(struct brcmf_pub* drvr, int32_t bsscfgidx, bool rtnl_locked) {
  struct brcmf_if* ifp;

  ifp = drvr->iflist[bsscfgidx];
  drvr->iflist[bsscfgidx] = NULL;
  if (!ifp) {
    BRCMF_ERR("Null interface, bsscfgidx=%d\n", bsscfgidx);
    return;
  }
  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifp->ifidx);
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
  BRCMF_DBG(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", ifp->bsscfgidx, ifp->ifidx);
  brcmf_proto_del_if(ifp->drvr, ifp);
  brcmf_del_if(ifp->drvr, ifp->bsscfgidx, rtnl_locked);
}

zx_status_t brcmf_attach(brcmf_pub* drvr) {
  BRCMF_DBG(TRACE, "Enter\n");

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
  struct brcmf_if* p2p_ifp;
  zx_status_t err;

  BRCMF_DBG(TRACE, "Enter\n");

  /* add primary networking interface */
  // TODO(WLAN-740): Name uniqueness
  err = brcmf_add_if(drvr, 0, 0, false, "wlan", NULL, &ifp);
  if (err != ZX_OK) {
    return err;
  }
  p2p_ifp = NULL;

  /* signal bus ready */
  brcmf_bus_change_state(bus_if, BRCMF_BUS_UP);
  /* Bus is ready, do any initialization */
  ret = brcmf_c_preinit_dcmds(ifp);
  if (ret != ZX_OK) {
    goto fail;
  }

  /* assure we have chipid before feature attach */
  if (!bus_if->chip) {
    bus_if->chip = drvr->revinfo.chipnum;
    bus_if->chiprev = drvr->revinfo.chiprev;
    BRCMF_DBG(INFO, "firmware revinfo: chip %x (%d) rev %d\n", bus_if->chip, bus_if->chip,
              bus_if->chiprev);
  }
  brcmf_feat_attach(drvr);

  ret = brcmf_proto_init_done(drvr);
  if (ret != ZX_OK) {
    goto fail;
  }

  brcmf_proto_add_if(drvr, ifp);

  drvr->config = brcmf_cfg80211_attach(drvr);
  if (drvr->config == NULL) {
    ret = ZX_ERR_IO;
    goto fail;
  }

  ret = brcmf_net_attach(ifp, false);

  if ((ret == ZX_OK) && (drvr->settings->p2p_enable)) {
    p2p_ifp = drvr->iflist[1];
    if (p2p_ifp) {
      ret = brcmf_net_p2p_attach(p2p_ifp);
    }
  }

  if (ret != ZX_OK) {
    goto fail;
  }

  return ZX_OK;

fail:
  BRCMF_ERR("failed: %d\n", ret);
  if (drvr->config) {
    brcmf_cfg80211_detach(drvr->config);
    drvr->config = NULL;
  }
  brcmf_net_detach(ifp->ndev, false);
  if (p2p_ifp) {
    brcmf_net_detach(p2p_ifp->ndev, false);
  }
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
  BRCMF_DBG(TRACE, "Enter\n");

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
                                 int32_t* fwerr_ptr) {
  struct brcmf_if* ifp = drvr->iflist[0];

  return brcmf_fil_iovar_data_set(ifp, name, data, len, fwerr_ptr);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if* ifp) { return ifp->pend_8021x_cnt.load(); }

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
  strlcpy(dev->name, name, sizeof(dev->name));
  return dev;
}

void brcmf_free_net_device(struct net_device* dev) {
  if (dev != NULL) {
    free(dev->priv);
    free(dev);
  }
}

void brcmf_enable_tx(struct net_device* dev) {
  BRCMF_DBG(INFO, " * * NOTE: brcmf_enable_tx called. Enable TX. (Was netif_wake_queue)\n");
}
void brcmf_netdev_wait_pend8021x(struct brcmf_if* ifp) {
  zx_status_t result;

  sync_completion_reset(&ifp->pend_8021x_wait);
  if (!brcmf_get_pend_8021x_cnt(ifp)) {
    return;
  }
  result = sync_completion_wait(&ifp->pend_8021x_wait, ZX_MSEC(MAX_WAIT_FOR_8021X_TX_MSEC));

  if (result != ZX_OK) {
    BRCMF_ERR("Timed out waiting for no pending 802.1x packets\n");
  }
}

void brcmf_bus_change_state(struct brcmf_bus* bus, enum brcmf_bus_state state) {
  BRCMF_DBG(TRACE, "%d -> %d\n", bus->state, state);
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
        // BRCMF_DBG(INFO, "This code called netif_wake_queue(ndev)\n");
        // BRCMF_DBG(INFO, "  if netif_queue_stopped(ndev). Do the Fuchsia equivalent.\n");
      }
    }
  }
#endif
}
