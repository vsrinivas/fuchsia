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

#include <ddk/device.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/sdio.h>
#include <ddk/protocol/usb.h>
#include <netinet/if_ether.h>

#include <endian.h>
#include <pthread.h>
#include <stdatomic.h>
#include <threads.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "cfg80211.h"
#include "common.h"
#include "debug.h"
#include "device.h"
#include "feature.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "p2p.h"
#include "pcie.h"
#include "pno.h"
#include "proto.h"
#include "workqueue.h"

#define MAX_WAIT_FOR_8021X_TX_MSEC (950)

#define BRCMF_BSSIDX_INVALID -1

char* brcmf_ifname(struct brcmf_if* ifp) {
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
        brcmf_err("ifidx %d out of range\n", ifidx);
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

    if (enable) {
        mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
    } else {
        mode = 0;
    }

    /* Try to set and enable ARP offload feature, this may fail, then it  */
    /* is simply not supported and err 0 will be returned                 */
    err = brcmf_fil_iovar_int_set(ifp, "arp_ol", mode);
    if (err != ZX_OK) {
        brcmf_dbg(TRACE, "failed to set ARP offload mode to 0x%x, err = %d\n", mode, err);
    } else {
        err = brcmf_fil_iovar_int_set(ifp, "arpoe", enable);
        if (err != ZX_OK) {
            brcmf_dbg(TRACE, "failed to configure (%d) ARP offload err = %d\n", enable, err);
        } else {
            brcmf_dbg(TRACE, "successfully configured (%d) ARP offload to 0x%x\n", enable, mode);
        }
    }

    err = brcmf_fil_iovar_int_set(ifp, "ndoe", enable);
    if (err != ZX_OK) {
        brcmf_dbg(TRACE, "failed to configure (%d) ND offload err = %d\n", enable, err);
    } else {
        brcmf_dbg(TRACE, "successfully configured (%d) ND offload to 0x%x\n", enable, mode);
    }
}

static void _brcmf_set_multicast_list(struct work_struct* work) {
    struct brcmf_if* ifp;
    struct net_device* ndev;
    struct netdev_hw_addr* ha;
    uint32_t cmd_value, cnt;
    uint32_t cnt_le;
    char* buf;
    char* bufp;
    uint32_t buflen;
    zx_status_t err;

    ifp = containerof(work, struct brcmf_if, multicast_work);

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

    ndev = ifp->ndev;

    /* Determine initial value of allmulti flag */
    cmd_value = (ndev->flags & IFF_ALLMULTI) ? true : false;

    /* Send down the multicast list first. */
    cnt = netdev_mc_count(ndev);
    buflen = sizeof(cnt) + (cnt * ETH_ALEN);
    buf = malloc(buflen);
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

    err = brcmf_fil_iovar_data_set(ifp, "mcast_list", buf, buflen);
    if (err != ZX_OK) {
        brcmf_err("Setting mcast_list failed, %d\n", err);
        cmd_value = cnt ? true : cmd_value;
    }

    free(buf);

    /*
     * Now send the allmulti setting.  This is based on the setting in the
     * net_device flags, but might be modified above to be turned on if we
     * were trying to set some addresses and dongle rejected it...
     */
    err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value);
    if (err != ZX_OK) {
        brcmf_err("Setting allmulti failed, %d\n", err);
    }

    /*Finally, pick up the PROMISC flag */
    cmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
    err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value);
    if (err != ZX_OK) {
        brcmf_err("Setting BRCMF_C_SET_PROMISC failed, %d\n", err);
    }
    brcmf_configure_arp_nd_offload(ifp, !cmd_value);
}

#if IS_ENABLED(CONFIG_IPV6)
static void _brcmf_update_ndtable(struct work_struct* work) {
    struct brcmf_if* ifp;
    int i;
    zx_status_t ret;

    ifp = containerof(work, struct brcmf_if, ndoffload_work);

    /* clear the table in firmware */
    ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip_clear", NULL, 0);
    if (ret != ZX_OK) {
        brcmf_dbg(TRACE, "fail to clear nd ip table err:%d\n", ret);
        return;
    }

    for (i = 0; i < ifp->ipv6addr_idx; i++) {
        ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip", &ifp->ipv6_addr_tbl[i],
                                       sizeof(struct in6_addr));
        if (ret != ZX_OK) {
            brcmf_err("add nd ip err %s\n", zx_status_get_string(ret));
        }
    }
}
#else
static void _brcmf_update_ndtable(struct work_struct* work) {}
#endif

static zx_status_t brcmf_netdev_set_mac_address(struct net_device* ndev, void* addr) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct sockaddr* sa = (struct sockaddr*)addr;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

    err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", sa->sa_data, ETH_ALEN);
    if (err != ZX_OK) {
        brcmf_err("Setting cur_etheraddr failed, %d\n", err);
    } else {
        brcmf_dbg(TRACE, "updated to %pM\n", sa->sa_data);
        memcpy(ifp->mac_addr, sa->sa_data, ETH_ALEN);
        memcpy(ifp->ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
    }
    return err;
}

static void brcmf_netdev_set_multicast_list(struct net_device* ndev) {
    struct brcmf_if* ifp = ndev_to_if(ndev);

    workqueue_schedule_default(&ifp->multicast_work);
}

static netdev_tx_t brcmf_netdev_start_xmit(struct brcmf_netbuf* netbuf, struct net_device* ndev) {
    zx_status_t ret;
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_pub* drvr = ifp->drvr;
    struct ethhdr* eh;
    int head_delta;

    brcmf_dbg(DATA, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

    /* Can the device send data? */
    if (drvr->bus_if->state != BRCMF_BUS_UP) {
        brcmf_err("xmit rejected state=%d\n", drvr->bus_if->state);
        netif_stop_queue(ndev);
        brcmf_netbuf_free(netbuf);
        ret = ZX_ERR_UNAVAILABLE;
        goto done;
    }

    /* Make sure there's enough writeable headroom */
    if (brcmf_netbuf_head_space(netbuf) < drvr->hdrlen) {
        head_delta = max((int)(drvr->hdrlen - brcmf_netbuf_head_space(netbuf)), 0);

        brcmf_dbg(INFO, "%s: insufficient headroom (%d)\n", brcmf_ifname(ifp), head_delta);
        atomic_fetch_add(&drvr->bus_if->stats.pktcowed, 1);
        ret = brcmf_netbuf_grow_realloc(netbuf, ALIGN(head_delta, NET_NETBUF_PAD), 0);
        if (ret != ZX_OK) {
            brcmf_err("%s: failed to expand headroom\n", brcmf_ifname(ifp));
            atomic_fetch_add(&drvr->bus_if->stats.pktcow_failed, 1);
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
        atomic_fetch_add(&ifp->pend_8021x_cnt, 1);
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

    /* Return ok: we always eat the packet */
    return NETDEV_TX_OK;
}

void brcmf_txflowblock_if(struct brcmf_if* ifp, enum brcmf_netif_stop_reason reason, bool state) {
    if (!ifp || !ifp->ndev) {
        return;
    }

    brcmf_dbg(TRACE, "enter: bsscfgidx=%d stop=0x%X reason=%d state=%d\n", ifp->bsscfgidx,
              ifp->netif_stop, reason, state);

    //spin_lock_irqsave(&ifp->netif_stop_lock, flags);
    pthread_mutex_lock(&irq_callback_lock);

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
    //spin_unlock_irqrestore(&ifp->netif_stop_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

void brcmf_netif_rx(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {
    if (netbuf->pkt_type == ADDRESSED_TO_MULTICAST) {
        ifp->ndev->stats.multicast++;
    }

    if (!(ifp->ndev->flags & IFF_UP)) {
        brcmu_pkt_buf_free_netbuf(netbuf);
        return;
    }

    ifp->ndev->stats.rx_bytes += netbuf->len;
    ifp->ndev->stats.rx_packets++;

    brcmf_dbg(DATA, "rx proto=0x%X\n", be16toh(netbuf->protocol));
    if (in_interrupt()) {
        netif_rx(netbuf);
    } else {
        /* If the receive is not processed inside an ISR,
         * the softirqd must be woken explicitly to service
         * the NET_RX_SOFTIRQ.  This is handled by netif_rx_ni().
         */
        netif_rx_ni(netbuf);
    }
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

    // TODO(cphoenix): Double-check (be paranoid) that these side effects of eth_type_trans()
    // are not used in this code.
    // - netbuf->dev
    // Also double-check that we're not using DSA in our net device (whatever that is)
    // and that we don't worry about "older Novell" IPX.
    // TODO(cphoenix): This is an ugly hack, probably buggy, to replace some of eth_type_trans.
    // See https://elixir.bootlin.com/linux/v4.17-rc7/source/net/ethernet/eth.c#L156
    brcmf_dbg(TEMP, "Packet header:");
    brcmf_hexdump(netbuf->data, min(netbuf->len, 32));
    brcmf_alphadump(netbuf->data, netbuf->len);
    if (address_is_multicast(netbuf->data)) {
        if (address_is_broadcast(netbuf->data)) {
            netbuf->pkt_type = ADDRESSED_TO_BROADCAST;
        } else {
            netbuf->pkt_type = ADDRESSED_TO_MULTICAST;
        }
    } else if (memcmp(netbuf->data, (*ifp)->ndev->dev_addr, 6)) {
        netbuf->pkt_type = ADDRESSED_TO_OTHER_HOST;
    }
    struct ethhdr* header = (struct ethhdr*)netbuf->data;
    if (header->h_proto >= ETH_P_802_3_MIN) {
        netbuf->protocol = header->h_proto;
    } else {
        netbuf->protocol = htobe16(ETH_P_802_2);
    }
    netbuf->eth_header = netbuf->data;
    if (netbuf->len >= 14) {
        brcmf_netbuf_shrink_head(netbuf, 14);
    }
    //netbuf->protocol = eth_type_trans(netbuf, (*ifp)->ndev);
    return ZX_OK;
}

void brcmf_rx_frame(struct brcmf_device* dev, struct brcmf_netbuf* netbuf, bool handle_event) {
    struct brcmf_if* ifp;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    brcmf_dbg(DATA, "Enter: %s: rxp=%p\n", device_get_name(dev->zxdev), netbuf);

    if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
        brcmf_dbg(TEMP, "hdrpull returned nonzero");
        return;
    }

    if (brcmf_proto_is_reorder_netbuf(netbuf)) {
        brcmf_proto_rxreorder(ifp, netbuf);
    } else {
        /* Process special event packets */
        if (handle_event) {
            brcmf_fweh_process_netbuf(ifp->drvr, netbuf);
        }

        brcmf_netif_rx(ifp, netbuf);
    }
}

void brcmf_rx_event(struct brcmf_device* dev, struct brcmf_netbuf* netbuf) {
    struct brcmf_if* ifp;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    brcmf_dbg(EVENT, "Enter: %s: rxp=%p\n", device_get_name(dev->zxdev), netbuf);

    if (brcmf_rx_hdrpull(drvr, netbuf, &ifp)) {
        return;
    }

    brcmf_fweh_process_netbuf(ifp->drvr, netbuf);
    brcmu_pkt_buf_free_netbuf(netbuf);
}

void brcmf_txfinalize(struct brcmf_if* ifp, struct brcmf_netbuf* txp, bool success) {
    struct ethhdr* eh;
    uint16_t type;

    eh = (struct ethhdr*)(txp->data);
    type = be16toh(eh->h_proto);

    if (type == ETH_P_PAE) {
        if (atomic_fetch_sub(&ifp->pend_8021x_cnt, 1) == 1) {
            completion_signal(&ifp->pend_8021x_wait);
        }
    }

    if (!success) {
        ifp->ndev->stats.tx_errors++;
    }

    brcmu_pkt_buf_free_netbuf(txp);
}

static void brcmf_ethtool_get_drvinfo(struct net_device* ndev, struct ethtool_drvinfo* info) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_pub* drvr = ifp->drvr;
    char drev[BRCMU_DOTREV_LEN] = "n/a";

    if (drvr->revinfo.result == ZX_OK) {
        brcmu_dotrev_str(drvr->revinfo.driverrev, drev);
    }
    strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
    strlcpy(info->version, drev, sizeof(info->version));
    strlcpy(info->fw_version, drvr->fwver, sizeof(info->fw_version));
    strlcpy(info->bus_info, device_get_name(drvr->bus_if->dev->zxdev), sizeof(info->bus_info));
}

static const struct ethtool_ops brcmf_ethtool_ops = {
    .get_drvinfo = brcmf_ethtool_get_drvinfo,
};

static zx_status_t brcmf_netdev_stop(struct net_device* ndev) {
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

    brcmf_cfg80211_down(ndev);

    brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear", NULL, 0);

    brcmf_net_setcarrier(ifp, false);

    return ZX_OK;
}

zx_status_t brcmf_netdev_open(struct net_device* ndev) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_pub* drvr = ifp->drvr;
    struct brcmf_bus* bus_if = drvr->bus_if;
    uint32_t toe_ol;

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

    /* If bus is not ready, can't continue */
    if (bus_if->state != BRCMF_BUS_UP) {
        brcmf_err("failed bus is not ready\n");
        return ZX_ERR_UNAVAILABLE;
    }

    atomic_store(&ifp->pend_8021x_cnt, 0);

    /* Get current TOE mode from dongle */
    if (brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_ol) == ZX_OK &&
            (toe_ol & TOE_TX_CSUM_OL) != 0) {
        ndev->features |= NETIF_F_IP_CSUM;
    } else {
        ndev->features &= ~NETIF_F_IP_CSUM;
    }

    if (brcmf_cfg80211_up(ndev) != ZX_OK) {
        brcmf_err("failed to bring up cfg80211\n");
        return ZX_ERR_IO;
    }

    /* Clear, carrier, set when connected or AP mode. */
    brcmf_dbg(TEMP, "* * Would have called netif_carrier_off(ndev);");
    return ZX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_pri = {
    .ndo_open = brcmf_netdev_open,
    .ndo_stop = brcmf_netdev_stop,
    .ndo_start_xmit = brcmf_netdev_start_xmit,
    .ndo_set_mac_address = brcmf_netdev_set_mac_address,
    .ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
};

zx_status_t brcmf_net_attach(struct brcmf_if* ifp, bool rtnl_locked) {
    struct brcmf_pub* drvr = ifp->drvr;
    struct net_device* ndev;
    zx_status_t result;

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx, ifp->mac_addr);
    ndev = ifp->ndev;

    /* set appropriate operations */
    ndev->netdev_ops = &brcmf_netdev_ops_pri;

    ndev->needed_headroom += drvr->hdrlen;
    ndev->ethtool_ops = &brcmf_ethtool_ops;

    /* set the mac address & netns */
    memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
    brcmf_dbg(TEMP, " * * Tried to call dev_net_set(ndev, wiphy_net(cfg_to_wiphy(drvr->config)));");
    brcmf_dbg(TEMP, "  to 'set the nd_net of net_device to the specified net namespace'");
    brcmf_dbg(TEMP, "  (Note to self: see Downloads/linuxkernnet.pdf)");

    workqueue_init_work(&ifp->multicast_work, _brcmf_set_multicast_list);
    workqueue_init_work(&ifp->ndoffload_work, _brcmf_update_ndtable);

    ndev->priv_destructor = brcmf_free_net_device_vif;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "broadcom-wlan",
        .ctx = NULL,
        .ops = &device_ops,
        .proto_id = ZX_PROTOCOL_WLANPHY,
        .proto_ops = NULL,
    };

    struct brcmf_device* device = ifp->drvr->bus_if->dev;
    result = device_add(device->zxdev, &args, &device->child_zxdev);
    if (result != ZX_OK) {
        brcmf_err("Failed to device_add");
        goto fail;
    }

    ndev->priv_destructor = brcmf_free_net_device_vif;
    brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);
    return ZX_OK;

fail:
    drvr->iflist[ifp->bsscfgidx] = NULL;
    ndev->netdev_ops = NULL;
    return ZX_ERR_IO_NOT_PRESENT;
}

static void brcmf_net_detach(struct net_device* ndev, bool rtnl_locked) {
    if (ndev->reg_state == NETREG_REGISTERED) {
        // TODO(cphoenix): Tell devhost we're not valid
        brcmf_dbg(TEMP, "* * Need to tell devhost we're not valid anymore");
        /*if (rtnl_locked) {
            unregister_netdevice(ndev);
        } else {
            unregister_netdev(ndev);
        }*/
    } else {
        brcmf_free_net_device_vif(ndev);
        brcmf_free_net_device(ndev);
    }
}

void brcmf_net_setcarrier(struct brcmf_if* ifp, bool on) {
    struct net_device* ndev;

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d carrier=%d\n", ifp->bsscfgidx, on);

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

static zx_status_t brcmf_net_p2p_open(struct net_device* ndev) {
    brcmf_dbg(TRACE, "Enter\n");

    return brcmf_cfg80211_up(ndev);
}

static zx_status_t brcmf_net_p2p_stop(struct net_device* ndev) {
    brcmf_dbg(TRACE, "Enter\n");

    return brcmf_cfg80211_down(ndev);
}

static netdev_tx_t brcmf_net_p2p_start_xmit(struct brcmf_netbuf* netbuf, struct net_device* ndev) {
    if (netbuf) {
        brcmf_netbuf_free(netbuf);
    }

    return NETDEV_TX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_p2p = {
    .ndo_open = brcmf_net_p2p_open,
    .ndo_stop = brcmf_net_p2p_stop,
    .ndo_start_xmit = brcmf_net_p2p_start_xmit
};

static zx_status_t brcmf_net_p2p_attach(struct brcmf_if* ifp) {
    struct net_device* ndev;

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx, ifp->mac_addr);
    ndev = ifp->ndev;

    ndev->netdev_ops = &brcmf_netdev_ops_p2p;

    /* set the mac address */
    memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);

    brcmf_err("* * Tried to register_netdev(ndev); do the ZX thing instead.");
    // TODO(cphoenix): Add back the appropriate "fail:" code
    // If register_netdev failed, goto fail;

    brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

    return ZX_OK;

//fail:
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

    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifidx);

    ifp = drvr->iflist[bsscfgidx];
    /*
     * Delete the existing interface before overwriting it
     * in case we missed the BRCMF_E_IF_DEL event.
     */
    if (ifp) {
        if (ifidx) {
            brcmf_err("ERROR: netdev:%s already exists\n", ifp->ndev->name);
            netif_stop_queue(ifp->ndev);
            brcmf_net_detach(ifp->ndev, false);
            drvr->iflist[bsscfgidx] = NULL;
        } else {
            brcmf_dbg(INFO, "netdev:%s ignore IF event\n", ifp->ndev->name);
            return ZX_ERR_INVALID_ARGS;
        }
    }

    if (!drvr->settings->p2p_enable && is_p2pdev) {
        /* this is P2P_DEVICE interface */
        brcmf_dbg(INFO, "allocate non-netdev interface\n");
        ifp = calloc(1, sizeof(*ifp));
        if (!ifp) {
            return ZX_ERR_NO_MEMORY;
        }
    } else {
        brcmf_dbg(INFO, "allocate netdev interface\n");
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

    ifp->pend_8021x_wait = COMPLETION_INIT;
    //spin_lock_init(&ifp->netif_stop_lock);

    if (mac_addr != NULL) {
        memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);
    }

    brcmf_dbg(TRACE, " ==== if:%s (%pM) created ===\n", name, ifp->mac_addr);
    if (if_out) {
        *if_out = ifp;
    }
    brcmf_dbg(TRACE, "Exit");
    return ZX_OK;
}

static void brcmf_del_if(struct brcmf_pub* drvr, int32_t bsscfgidx, bool rtnl_locked) {
    struct brcmf_if* ifp;

    ifp = drvr->iflist[bsscfgidx];
    drvr->iflist[bsscfgidx] = NULL;
    if (!ifp) {
        brcmf_err("Null interface, bsscfgidx=%d\n", bsscfgidx);
        return;
    }
    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifp->ifidx);
    if (drvr->if2bss[ifp->ifidx] == bsscfgidx) {
        drvr->if2bss[ifp->ifidx] = BRCMF_BSSIDX_INVALID;
    }
    if (ifp->ndev) {
        if (bsscfgidx == 0) {
            if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
                rtnl_lock();
                brcmf_netdev_stop(ifp->ndev);
                rtnl_unlock();
            }
        } else {
            netif_stop_queue(ifp->ndev);
        }

        if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
            workqueue_cancel_work(&ifp->multicast_work);
            workqueue_cancel_work(&ifp->ndoffload_work);
        }
        brcmf_net_detach(ifp->ndev, rtnl_locked);
    } else {
        /* Only p2p device interfaces which get dynamically created
         * end up here. In this case the p2p module should be informed
         * about the removal of the interface within the firmware. If
         * not then p2p commands towards the firmware will cause some
         * serious troublesome side effects. The p2p module will clean
         * up the ifp if needed.
         */
        brcmf_p2p_ifp_removed(ifp, rtnl_locked);
        free(ifp);
    }
}

void brcmf_remove_interface(struct brcmf_if* ifp, bool rtnl_locked) {
    if (!ifp || WARN_ON(ifp->drvr->iflist[ifp->bsscfgidx] != ifp)) {
        return;
    }
    brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", ifp->bsscfgidx, ifp->ifidx);
    brcmf_proto_del_if(ifp->drvr, ifp);
    brcmf_del_if(ifp->drvr, ifp->bsscfgidx, rtnl_locked);
}

static zx_status_t brcmf_psm_watchdog_notify(struct brcmf_if* ifp,
                                             const struct brcmf_event_msg* evtmsg, void* data) {
    zx_status_t err;

    brcmf_dbg(TRACE, "enter: bsscfgidx=%d\n", ifp->bsscfgidx);

    brcmf_err("PSM's watchdog has fired!\n");

    err = brcmf_debug_create_memdump(ifp->drvr->bus_if, data, evtmsg->datalen);
    if (err != ZX_OK) {
        brcmf_err("Failed to get memory dump, %d\n", err);
    }

    return err;
}

#ifdef CONFIG_INET
#define ARPOL_MAX_ENTRIES 8
static int brcmf_inetaddr_changed(struct notifier_block* nb, unsigned long action, void* data) {
    struct brcmf_pub* drvr = containerof(nb, struct brcmf_pub, inetaddr_notifier);
    struct in_ifaddr* ifa = data;
    struct net_device* ndev = ifa->ifa_dev->dev;
    struct brcmf_if* ifp;
    int idx, i;
    zx_status_t ret;
    uint32_t val;
    __be32 addr_table[ARPOL_MAX_ENTRIES] = {0};

    /* Find out if the notification is meant for us */
    for (idx = 0; idx < BRCMF_MAX_IFS; idx++) {
        ifp = drvr->iflist[idx];
        if (ifp && ifp->ndev == ndev) {
            break;
        }
        if (idx == BRCMF_MAX_IFS - 1) {
            return NOTIFY_DONE;
        }
    }

    /* check if arp offload is supported */
    ret = brcmf_fil_iovar_int_get(ifp, "arpoe", &val);
    if (ret != ZX_OK) {
        return NOTIFY_OK;
    }

    /* old version only support primary index */
    ret = brcmf_fil_iovar_int_get(ifp, "arp_version", &val);
    if (ret != ZX_OK) {
        val = 1;
    }
    if (val == 1) {
        ifp = drvr->iflist[0];
    }

    /* retrieve the table from firmware */
    ret = brcmf_fil_iovar_data_get(ifp, "arp_hostip", addr_table, sizeof(addr_table));
    if (ret != ZX_OK) {
        brcmf_err("fail to get arp ip table err:%d\n", ret);
        return NOTIFY_OK;
    }

    for (i = 0; i < ARPOL_MAX_ENTRIES; i++)
        if (ifa->ifa_address == addr_table[i]) {
            break;
        }

    switch (action) {
    case NETDEV_UP:
        if (i == ARPOL_MAX_ENTRIES) {
            brcmf_dbg(TRACE, "add %pI4 to arp table\n", &ifa->ifa_address);
            /* set it directly */
            ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip", &ifa->ifa_address,
                                           sizeof(ifa->ifa_address));
            if (ret != ZX_OK) {
                brcmf_err("add arp ip err %s\n", zx_status_get_string(ret));
            }
        }
        break;
    case NETDEV_DOWN:
        if (i < ARPOL_MAX_ENTRIES) {
            addr_table[i] = 0;
            brcmf_dbg(TRACE, "remove %pI4 from arp table\n", &ifa->ifa_address);
            /* clear the table in firmware */
            ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear", NULL, 0);
            if (ret != ZX_OK) {
                brcmf_err("fail to clear arp ip table err:%d\n", ret);
                return NOTIFY_OK;
            }
            for (i = 0; i < ARPOL_MAX_ENTRIES; i++) {
                if (addr_table[i] == 0) {
                    continue;
                }
                ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip", &addr_table[i],
                                               sizeof(addr_table[i]));
                if (ret != ZX_OK) {
                    brcmf_err("add arp ip err %s\n", zx_status_get_string(ret));
                }
            }
        }
        break;
    default:
        break;
    }

    return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int brcmf_inet6addr_changed(struct notifier_block* nb, unsigned long action, void* data) {
    struct brcmf_pub* drvr = containerof(nb, struct brcmf_pub, inet6addr_notifier);
    struct inet6_ifaddr* ifa = data;
    struct brcmf_if* ifp;
    int i;
    struct in6_addr* table;

    /* Only handle primary interface */
    ifp = drvr->iflist[0];
    if (!ifp) {
        return NOTIFY_DONE;
    }
    if (ifp->ndev != ifa->idev->dev) {
        return NOTIFY_DONE;
    }

    table = ifp->ipv6_addr_tbl;
    for (i = 0; i < NDOL_MAX_ENTRIES; i++)
        if (ipv6_addr_equal(&ifa->addr, &table[i])) {
            break;
        }

    switch (action) {
    case NETDEV_UP:
        if (i == NDOL_MAX_ENTRIES) {
            if (ifp->ipv6addr_idx < NDOL_MAX_ENTRIES) {
                table[ifp->ipv6addr_idx++] = ifa->addr;
            } else {
                for (i = 0; i < NDOL_MAX_ENTRIES - 1; i++) {
                    table[i] = table[i + 1];
                }
                table[NDOL_MAX_ENTRIES - 1] = ifa->addr;
            }
        }
        break;
    case NETDEV_DOWN:
        if (i < NDOL_MAX_ENTRIES) {
            for (; i < ifp->ipv6addr_idx - 1; i++) {
                table[i] = table[i + 1];
            }
            memset(&table[i], 0, sizeof(table[i]));
            ifp->ipv6addr_idx--;
        }
        break;
    default:
        break;
    }

    workqueue_schedule_default(&ifp->ndoffload_work);

    return NOTIFY_OK;
}
#endif

zx_status_t brcmf_attach(struct brcmf_device* dev, struct brcmf_mp_device* settings) {
    struct brcmf_pub* drvr = NULL;
    zx_status_t ret = ZX_OK;
    int i;

    brcmf_dbg(TRACE, "Enter\n");

    /* Allocate primary brcmf_info */
    drvr = calloc(1, sizeof(struct brcmf_pub));
    if (!drvr) {
        return ZX_ERR_NO_MEMORY;
    }

    for (i = 0; i < (int)ARRAY_SIZE(drvr->if2bss); i++) {
        drvr->if2bss[i] = BRCMF_BSSIDX_INVALID;
    }

    mtx_init(&drvr->proto_block, mtx_plain);

    /* Link to bus module */
    drvr->hdrlen = 0;
    drvr->bus_if = dev_to_bus(dev);
    drvr->bus_if->drvr = drvr;
    drvr->settings = settings;

    /* attach debug facilities */
    brcmf_debug_attach(drvr);

    /* Attach and link in the protocol */
    ret = brcmf_proto_attach(drvr);
    if (ret != ZX_OK) {
        brcmf_err("brcmf_prot_attach failed\n");
        goto fail;
    }

    /* Attach to events important for core code */
    brcmf_fweh_register(drvr, BRCMF_E_PSM_WATCHDOG, brcmf_psm_watchdog_notify);

    /* attach firmware event handler */
    brcmf_fweh_attach(drvr);

    return ret;

fail:
    brcmf_detach(dev);

    return ret;
}

static zx_status_t brcmf_revinfo_read(struct seq_file* s, void* data) {
    struct brcmf_bus* bus_if = dev_to_bus(s->private);
    struct brcmf_rev_info* ri = &bus_if->drvr->revinfo;
    char drev[BRCMU_DOTREV_LEN];
    char brev[BRCMU_BOARDREV_LEN];

    seq_printf(s, "vendorid: 0x%04x\n", ri->vendorid);
    seq_printf(s, "deviceid: 0x%04x\n", ri->deviceid);
    seq_printf(s, "radiorev: %s\n", brcmu_dotrev_str(ri->radiorev, drev));
    seq_printf(s, "chipnum: %u (%x)\n", ri->chipnum, ri->chipnum);
    seq_printf(s, "chiprev: %u\n", ri->chiprev);
    seq_printf(s, "chippkg: %u\n", ri->chippkg);
    seq_printf(s, "corerev: %u\n", ri->corerev);
    seq_printf(s, "boardid: 0x%04x\n", ri->boardid);
    seq_printf(s, "boardvendor: 0x%04x\n", ri->boardvendor);
    seq_printf(s, "boardrev: %s\n", brcmu_boardrev_str(ri->boardrev, brev));
    seq_printf(s, "driverrev: %s\n", brcmu_dotrev_str(ri->driverrev, drev));
    seq_printf(s, "ucoderev: %u\n", ri->ucoderev);
    seq_printf(s, "bus: %u\n", ri->bus);
    seq_printf(s, "phytype: %u\n", ri->phytype);
    seq_printf(s, "phyrev: %u\n", ri->phyrev);
    seq_printf(s, "anarev: %u\n", ri->anarev);
    seq_printf(s, "nvramrev: %08x\n", ri->nvramrev);

    seq_printf(s, "clmver: %s\n", bus_if->drvr->clmver);

    return ZX_OK;
}

zx_status_t brcmf_bus_started(struct brcmf_device* dev) {
    zx_status_t ret = ZX_ERR_IO;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;
    struct brcmf_if* ifp;
    struct brcmf_if* p2p_ifp;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter");

    /* add primary networking interface */
    // TODO(NET-974): Name uniqueness
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

    brcmf_debugfs_add_entry(drvr, "revinfo", brcmf_revinfo_read);

    /* assure we have chipid before feature attach */
    if (!bus_if->chip) {
        bus_if->chip = drvr->revinfo.chipnum;
        bus_if->chiprev = drvr->revinfo.chiprev;
        brcmf_dbg(INFO, "firmware revinfo: chip %x (%d) rev %d\n", bus_if->chip, bus_if->chip,
                  bus_if->chiprev);
    }
    brcmf_feat_attach(drvr);

    ret = brcmf_proto_init_done(drvr);
    if (ret != ZX_OK) {
        goto fail;
    }

    brcmf_proto_add_if(drvr, ifp);

    drvr->config = brcmf_cfg80211_attach(drvr, bus_if->dev, drvr->settings->p2p_enable);
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

#ifdef CONFIG_INET
    drvr->inetaddr_notifier.notifier_call = brcmf_inetaddr_changed;
    ret = register_inetaddr_notifier(&drvr->inetaddr_notifier);
    if (ret != ZX_OK) {
        goto fail;
    }

#if IS_ENABLED(CONFIG_IPV6)
    drvr->inet6addr_notifier.notifier_call = brcmf_inet6addr_changed;
    ret = register_inet6addr_notifier(&drvr->inet6addr_notifier);
    if (ret != ZX_OK) {
        unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
        goto fail;
    }
#endif
#endif /* CONFIG_INET */

    return ZX_OK;

fail:
    brcmf_err("failed: %d\n", ret);
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

void brcmf_bus_add_txhdrlen(struct brcmf_device* dev, uint len) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    if (drvr) {
        drvr->hdrlen += len;
    }
}

void brcmf_dev_reset(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    if (drvr == NULL) {
        return;
    }

    if (drvr->iflist[0]) {
        brcmf_fil_cmd_int_set(drvr->iflist[0], BRCMF_C_TERMINATED, 1);
    }
}

void brcmf_detach(struct brcmf_device* dev) {
    int32_t i;
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    brcmf_dbg(TRACE, "Enter\n");

    if (drvr == NULL) {
        return;
    }

#ifdef CONFIG_INET
    unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
#endif

#if IS_ENABLED(CONFIG_IPV6)
    unregister_inet6addr_notifier(&drvr->inet6addr_notifier);
#endif

    /* stop firmware event handling */
    brcmf_fweh_detach(drvr);
    if (drvr->config) {
        brcmf_p2p_detach(&drvr->config->p2p);
    }

    brcmf_bus_change_state(bus_if, BRCMF_BUS_DOWN);

    /* make sure primary interface removed last */
    for (i = BRCMF_MAX_IFS - 1; i > -1; i--) {
        brcmf_remove_interface(drvr->iflist[i], false);
    }

    brcmf_cfg80211_detach(drvr->config);

    brcmf_bus_stop(drvr->bus_if);

    brcmf_proto_detach(drvr);

    brcmf_debug_detach(drvr);
    bus_if->drvr = NULL;
    free(drvr);
}

zx_status_t brcmf_iovar_data_set(struct brcmf_device* dev, char* name, void* data, uint32_t len) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_if* ifp = bus_if->drvr->iflist[0];

    return brcmf_fil_iovar_data_set(ifp, name, data, len);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if* ifp) {
    return atomic_load(&ifp->pend_8021x_cnt);
}

void brcmf_netdev_wait_pend8021x(struct brcmf_if* ifp) {
    zx_status_t result;

    completion_reset(&ifp->pend_8021x_wait);
    if (!brcmf_get_pend_8021x_cnt(ifp)) {
        return;
    }
    result = completion_wait(&ifp->pend_8021x_wait, ZX_MSEC(MAX_WAIT_FOR_8021X_TX_MSEC));

    if (result != ZX_OK) {
        brcmf_err("Timed out waiting for no pending 802.1x packets\n");
    }
}

void brcmf_bus_change_state(struct brcmf_bus* bus, enum brcmf_bus_state state) {
    struct brcmf_pub* drvr = bus->drvr;
    struct net_device* ndev;
    int ifidx;

    brcmf_dbg(TRACE, "%d -> %d\n", bus->state, state);
    bus->state = state;

    if (state == BRCMF_BUS_UP) {
        for (ifidx = 0; ifidx < BRCMF_MAX_IFS; ifidx++) {
            if ((drvr->iflist[ifidx]) && (drvr->iflist[ifidx]->ndev)) {
                ndev = drvr->iflist[ifidx]->ndev;
                // TODO(cphoenix): Implement Fuchsia equivalent of...
                // brcmf_dbg(INFO, "This code called netif_wake_queue(ndev)");
                // brcmf_dbg(INFO, "  if netif_queue_stopped(ndev). Do the Fuchsia equivalent.");
            }
        }
    }
}

zx_status_t brcmf_core_init(zx_device_t* device) {
    zx_status_t result;
    pthread_mutexattr_t pmutex_attributes;

    brcmf_dbg(TEMP, "brcmfmac: core_init was called\n");

    pthread_mutexattr_init(&pmutex_attributes);
    pthread_mutexattr_settype(&pmutex_attributes, PTHREAD_MUTEX_NORMAL | PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&irq_callback_lock, &pmutex_attributes);

#ifdef CONFIG_BRCMFMAC_PCIE
    pci_protocol_t pdev;
    result = device_get_protocol(device, ZX_PROTOCOL_PCI, &pdev);
    if (result == ZX_OK) {
        result = brcmf_pcie_register(device, &pdev);
        if (result != ZX_OK) {
            brcmf_err("PCIE driver registration failed, err=%d\n", result);
        }
        return result;
    }
#endif // CONFIG_BRCMFMAC_PCIE

#ifdef CONFIG_BRCMFMAC_USB
    usb_protocol_t udev;
    result = device_get_protocol(device, ZX_PROTOCOL_USB, &udev);
    if (result == ZX_OK) {
        result = brcmf_usb_register(device, &udev);
        if (result != ZX_OK) {
            brcmf_err("USB driver registration failed, err=%d\n", result);
        }
        return result;
    }
#endif // CONFIG_BRCMFMAC_USB

#ifdef CONFIG_BRCMFMAC_SDIO
    sdio_protocol_t sdev;
    result = device_get_protocol(device, ZX_PROTOCOL_SDIO, &sdev);
    if (result == ZX_OK) {
        result = brcmf_sdio_register(device, &sdev);
        if (result != ZX_OK) {
            brcmf_err("USB driver registration failed, err=%d\n", result);
        }
        return result;
    }
#endif // CONFIG_BRCMFMAC_SDIO
    return ZX_ERR_INTERNAL;
}

void brcmf_core_exit(void) {

#ifdef CONFIG_BRCMFMAC_SDIO
    brcmf_sdio_exit();
#endif
#ifdef CONFIG_BRCMFMAC_USB
    brcmf_usb_exit();
#endif
#ifdef CONFIG_BRCMFMAC_PCIE
    brcmf_pcie_exit();
#endif
}
