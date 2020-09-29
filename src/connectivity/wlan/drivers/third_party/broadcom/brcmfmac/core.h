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

/****************
 * Common types *
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CORE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CORE_H_

#include <lib/async/dispatcher.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <netinet/if_ether.h>
#include <threads.h>

#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>

#include <ddk/protocol/wlanif.h>
#include <ddk/protocol/wlanphyimpl.h>

#include "bus.h"
#include "fweh.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "workqueue.h"

#define TOE_TX_CSUM_OL 0x00000001
#define TOE_RX_CSUM_OL 0x00000002

#define BRCMF_BSSIDX_INVALID -1

/* For supporting multiple interfaces */
#define BRCMF_MAX_IFS 16

/* Small, medium and maximum buffer size for dcmd
 */
#define BRCMF_DCMD_SMLEN 256
#define BRCMF_DCMD_MEDLEN 1536
#define BRCMF_DCMD_MAXLEN 8192

/* IOCTL from host to device are limited in length. A device can only handle
 * ethernet frame size. This limitation is to be applied by protocol layer.
 */
#define BRCMF_TX_IOCTL_MAX_MSG_SIZE (ETH_FRAME_LEN + ETH_FCS_LEN)

#define BRCMF_AMPDU_RX_REORDER_MAXFLOWS 256

/* Length of firmware version string stored for
 * ethtool driver info which uses 32 bytes as well.
 */
#define BRCMF_DRIVER_FIRMWARE_VERSION_LEN 32

#define NDOL_MAX_ENTRIES 8

#define RSSI_HISTOGRAM_LEN 129

static inline bool address_is_multicast(const uint8_t* address) { return 1 & *address; }

static inline bool address_is_broadcast(const uint8_t* address) {
  static uint8_t all_ones[] = {255, 255, 255, 255, 255, 255};
  static_assert(ETH_ALEN == 6, "Oops");
  return !memcmp(address, all_ones, ETH_ALEN);
}

/**
 * struct brcmf_ampdu_rx_reorder - AMPDU receive reorder info
 *
 * @pktslots: dynamic allocated array for ordering AMPDU packets.
 * @flow_id: AMPDU flow identifier.
 * @cur_idx: last AMPDU index from firmware.
 * @exp_idx: expected next AMPDU index.
 * @max_idx: maximum amount of packets per AMPDU.
 * @pend_pkts: number of packets currently in @pktslots.
 */
struct brcmf_ampdu_rx_reorder {
  struct brcmf_netbuf** pktslots;
  uint8_t flow_id;
  uint8_t cur_idx;
  uint8_t exp_idx;
  uint8_t max_idx;
  uint8_t pend_pkts;
};

/* Forward decls for struct brcmf_pub (see below) */
struct brcmf_proto;     /* device communication protocol info */
struct brcmf_fws_info;  /* firmware signalling info */
struct brcmf_mp_device; /* module paramateres, device specific */

/*
 * struct brcmf_rev_info
 *
 * The result field stores the error code of the
 * revision info request from firmware. For the
 * other fields see struct brcmf_rev_info_le in
 * fwil_types.h
 */
struct brcmf_rev_info {
  zx_status_t result;
  struct brcmf_rev_info_le fwrevinfo;
};

/* Common structure for module and instance linkage */
struct brcmf_pub {
  zx_device_t* zxdev;
  async_dispatcher_t* dispatcher;
  std::recursive_mutex irq_callback_lock;

  /* Linkage ponters */
  struct brcmf_bus* bus_if;
  struct brcmf_proto* proto;
  struct brcmf_cfg80211_info* config;

  /* Internal brcmf items */
  uint hdrlen; /* Total BRCMF header length (proto + bus) */

  /* Dongle media info */
  char fwver[BRCMF_DRIVER_FIRMWARE_VERSION_LEN];

  struct brcmf_if* iflist[BRCMF_MAX_IFS];
  int32_t if2bss[BRCMF_MAX_IFS];

  std::mutex proto_block;
  unsigned char proto_buf[BRCMF_DCMD_MAXLEN];

  struct brcmf_fweh_info fweh;

  struct brcmf_ampdu_rx_reorder* reorder_flows[BRCMF_AMPDU_RX_REORDER_MAXFLOWS];

  uint32_t feat_flags;
  uint32_t chip_quirks;

  struct brcmf_rev_info revinfo;
#if !defined(NDEBUG)
  zx_handle_t dbgfs_dir;
#endif  // !defined(NDEBUG)

  struct brcmf_mp_device* settings;

  uint8_t clmver[BRCMF_DCMD_SMLEN];
};

/* forward declarations */
struct brcmf_cfg80211_vif;
struct brcmf_fws_mac_descriptor;

/**
 * enum brcmf_netif_stop_reason - reason for stopping netif queue.
 *
 * @BRCMF_NETIF_STOP_REASON_FWS_FC:
 *  netif stopped due to firmware signalling flow control.
 * @BRCMF_NETIF_STOP_REASON_FLOW:
 *  netif stopped due to flowring full.
 * @BRCMF_NETIF_STOP_REASON_DISCONNECTED:
 *  netif stopped due to not being connected (STA mode).
 */
enum brcmf_netif_stop_reason {
  BRCMF_NETIF_STOP_REASON_FWS_FC = BIT(0),
  BRCMF_NETIF_STOP_REASON_FLOW = BIT(1),
  BRCMF_NETIF_STOP_REASON_DISCONNECTED = BIT(2)
};

/**
 * struct brcmf_if - interface control information.
 *
 * @drvr: points to device related information.
 * @vif: points to cfg80211 specific interface information.
 * @ndev: associated network device.
 * @multicast_work: worker object for multicast provisioning.
 * @ndoffload_work: worker object for neighbor discovery offload configuration.
 * @fws_desc: interface specific firmware-signalling descriptor.
 * @ifidx: interface index in device firmware.
 * @bsscfgidx: index of bss associated with this interface.
 * @mac_addr: assigned mac address.
 * @netif_stop: bitmap indicates reason why netif queues are stopped.
 * //@netif_stop_lock: spinlock for update netif_stop from multiple sources.
 *  (replaced by irq_callback_lock)
 * @bss: information on current bss.
 * @pend_8021x_cnt: tracks outstanding number of 802.1x frames.
 * @pend_8021x_wait: used for signalling change in count.
 */
struct brcmf_if {
  struct brcmf_pub* drvr;
  struct brcmf_cfg80211_vif* vif;
  struct net_device* ndev;
  WorkItem multicast_work;
  WorkItem ndoffload_work;
  struct brcmf_fws_mac_descriptor* fws_desc;
  int ifidx;
  int32_t bsscfgidx;
  uint8_t mac_addr[ETH_ALEN];
  uint8_t netif_stop;
  struct wlanif_bss_description bss;
  // spinlock_t netif_stop_lock;
  std::atomic<int> pend_8021x_cnt;
  sync_completion_t pend_8021x_wait;
};

void brcmf_write_net_device_name(struct net_device* dev, const char* name);
struct net_device* brcmf_allocate_net_device(size_t priv_size, const char* name);
void brcmf_free_net_device(struct net_device* dev);
void brcmf_enable_tx(struct net_device* dev);
void brcmf_netdev_wait_pend8021x(struct brcmf_if* ifp);
void brcmf_netdev_start_xmit(struct net_device* ndev,
                             std::unique_ptr<wlan::brcmfmac::Netbuf> netbuf);

/* Return pointer to interface name */
const char* brcmf_ifname(struct brcmf_if* ifp);
struct brcmf_if* brcmf_get_ifp(struct brcmf_pub* drvr, int ifidx);
void brcmf_configure_arp_nd_offload(struct brcmf_if* ifp, bool enable);
void brcmf_netdev_set_multicast_list(struct net_device* ndev);
void brcmf_netdev_set_allmulti(struct net_device* ndev);
zx_status_t brcmf_net_attach(struct brcmf_if* ifp, bool rtnl_locked);
zx_status_t brcmf_add_if(struct brcmf_pub* drvr, int32_t bsscfgidx, int32_t ifidx, const char* name,
                         uint8_t* mac_addr, struct brcmf_if** if_out);
void brcmf_remove_interface(struct brcmf_if* ifp, bool rtnl_locked);
void brcmf_txflowblock_if(struct brcmf_if* ifp, enum brcmf_netif_stop_reason reason, bool state);
void brcmf_txfinalize(struct brcmf_if* ifp, const struct ethhdr* eh, bool success);
void brcmf_netif_rx(struct brcmf_if* ifp, const void* data, size_t size);
void brcmf_net_setcarrier(struct brcmf_if* ifp, bool on);

const char kPrimaryNetworkInterfaceName[] = "wlan";
#define NET_DEVICE_NAME_MAX_LEN 123

struct net_device {
  bool initialized_for_ap;
  bool scan_busy;
  bool multicast_promisc;
  bool is_up;
  uint64_t scan_txn_id;
  uint32_t scan_num_results;
  std::shared_mutex if_proto_lock;  // Used as RW-lock for if_proto.
  wlanif_impl_ifc_protocol_t if_proto;
  uint8_t dev_addr[ETH_ALEN];
  char name[NET_DEVICE_NAME_MAX_LEN];
  void* priv;
  struct {
    int tx_dropped;
    int tx_packets;
    int tx_bytes;
    int rx_packets;
    int rx_bytes;
    int multicast;
    int rx_errors;
    int tx_errors;
    // rssi histogram, index = -(rssi), For ex, -128 => 128....-1 => 1
    std::array<uint64_t, RSSI_HISTOGRAM_LEN> rssi_buckets;
    wlanif_mlme_stats_t mlme_stats;
    // To workaround a bug in firmware, store the last read value from signalling data.
    // Firmware does not update signalling data beyond the first packet in an ampdu.
    // TODO(karthikrish) This can be removed once the issue is resolved in firmware
    int8_t last_signal_rssi;
  } stats;
  zx::channel sme_channel;
  uint32_t features;
  uint32_t needed_headroom;
  void (*priv_destructor)(net_device*);
  int reg_state;
  int needs_free_net_device;
  int8_t last_known_rssi_dbm;
  int8_t last_known_snr_db;
};

/*
 * interface functions from common layer
 */

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
void brcmf_rx_frame(brcmf_pub* drvr, brcmf_netbuf* rxp, bool handle_event);
/* Receive async event packet from firmware. Callee disposes of rxp. */
void brcmf_rx_event(brcmf_pub* drvr, brcmf_netbuf* rxp);

/* Indication from bus module regarding presence/insertion of dongle. */
zx_status_t brcmf_attach(brcmf_pub* drvr);
/* Indication from bus module regarding removal/absence of dongle */
void brcmf_detach(brcmf_pub* drvr);
/* Indication from bus module that dongle should be reset */
void brcmf_dev_reset(brcmf_pub* drvr);

/* Configure the "global" bus state used by upper layers */
void brcmf_bus_change_state(brcmf_bus* bus, enum brcmf_bus_state state);

zx_status_t brcmf_bus_started(brcmf_pub* drvr);
zx_status_t brcmf_iovar_data_set(brcmf_pub* drvr, const char* name, void* data, uint32_t len,
                                 bcme_status_t* fwerr_ptr);
void brcmf_bus_add_txhdrlen(brcmf_pub* drvr, uint len);
zx_status_t brcmf_netdev_set_mac_address(struct net_device* ndev, uint8_t* addr);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CORE_H_
