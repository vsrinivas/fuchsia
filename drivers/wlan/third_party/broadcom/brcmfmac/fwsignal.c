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

//#include <linux/err.h>
//#include <linux/etherdevice.h>
//#include <linux/if_ether.h>
//#include <linux/jiffies.h>
//#include <linux/module.h>
//#include <linux/netdevice.h>
//#include <linux/skbuff.h>
//#include <linux/spinlock.h>
//#include <linux/types.h>
//#include <net/cfg80211.h>

#include "fwsignal.h"

#include <threads.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bcdc.h"
#include "bus.h"
#include "cfg80211.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "fweh.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "p2p.h"
#include "proto.h"
#include "workqueue.h"

/**
 * DOC: Firmware Signalling
 *
 * Firmware can send signals to host and vice versa, which are passed in the
 * data packets using TLV based header. This signalling layer is on top of the
 * BDC bus protocol layer.
 */

/*
 * single definition for firmware-driver flow control tlv's.
 *
 * each tlv is specified by BRCMF_FWS_TLV_DEF(name, ID, length).
 * A length value 0 indicates variable length tlv.
 */
#define BRCMF_FWS_TLV_DEFLIST                      \
    BRCMF_FWS_TLV_DEF(MAC_OPEN, 1, 1)              \
    BRCMF_FWS_TLV_DEF(MAC_CLOSE, 2, 1)             \
    BRCMF_FWS_TLV_DEF(MAC_REQUEST_CREDIT, 3, 2)    \
    BRCMF_FWS_TLV_DEF(TXSTATUS, 4, 4)              \
    BRCMF_FWS_TLV_DEF(PKTTAG, 5, 4)                \
    BRCMF_FWS_TLV_DEF(MACDESC_ADD, 6, 8)           \
    BRCMF_FWS_TLV_DEF(MACDESC_DEL, 7, 8)           \
    BRCMF_FWS_TLV_DEF(RSSI, 8, 1)                  \
    BRCMF_FWS_TLV_DEF(INTERFACE_OPEN, 9, 1)        \
    BRCMF_FWS_TLV_DEF(INTERFACE_CLOSE, 10, 1)      \
    BRCMF_FWS_TLV_DEF(FIFO_CREDITBACK, 11, 6)      \
    BRCMF_FWS_TLV_DEF(PENDING_TRAFFIC_BMP, 12, 2)  \
    BRCMF_FWS_TLV_DEF(MAC_REQUEST_PACKET, 13, 3)   \
    BRCMF_FWS_TLV_DEF(HOST_REORDER_RXPKTS, 14, 10) \
    BRCMF_FWS_TLV_DEF(TRANS_ID, 18, 6)             \
    BRCMF_FWS_TLV_DEF(COMP_TXSTATUS, 19, 1)        \
    BRCMF_FWS_TLV_DEF(FILLER, 255, 0)

/*
 * enum brcmf_fws_tlv_type - definition of tlv identifiers.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) BRCMF_FWS_TYPE_##name = id,
enum brcmf_fws_tlv_type { BRCMF_FWS_TLV_DEFLIST BRCMF_FWS_TYPE_INVALID };
#undef BRCMF_FWS_TLV_DEF

/*
 * enum brcmf_fws_tlv_len - definition of tlv lengths.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) BRCMF_FWS_TYPE_##name##_LEN = (len),
enum brcmf_fws_tlv_len { BRCMF_FWS_TLV_DEFLIST };
#undef BRCMF_FWS_TLV_DEF

// clang-format off

/* AMPDU rx reordering definitions */
#define BRCMF_RXREORDER_FLOWID_OFFSET 0
#define BRCMF_RXREORDER_MAXIDX_OFFSET 2
#define BRCMF_RXREORDER_FLAGS_OFFSET  4
#define BRCMF_RXREORDER_CURIDX_OFFSET 6
#define BRCMF_RXREORDER_EXPIDX_OFFSET 8

#define BRCMF_RXREORDER_DEL_FLOW     0x01
#define BRCMF_RXREORDER_FLUSH_ALL    0x02
#define BRCMF_RXREORDER_CURIDX_VALID 0x04
#define BRCMF_RXREORDER_EXPIDX_VALID 0x08
#define BRCMF_RXREORDER_NEW_HOLE     0x10

// clang-format-on

#ifdef DEBUG
/*
 * brcmf_fws_tlv_names - array of tlv names.
 */
#define BRCMF_FWS_TLV_DEF(name, id, len) {id, #name},
static struct {
    enum brcmf_fws_tlv_type id;
    const char* name;
} brcmf_fws_tlv_names[] = {BRCMF_FWS_TLV_DEFLIST};
#undef BRCMF_FWS_TLV_DEF

static const char* brcmf_fws_get_tlv_name(enum brcmf_fws_tlv_type id) {
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(brcmf_fws_tlv_names); i++)
        if (brcmf_fws_tlv_names[i].id == id) {
            return brcmf_fws_tlv_names[i].name;
        }

    return "INVALID";
}
#else
static const char* brcmf_fws_get_tlv_name(enum brcmf_fws_tlv_type id) {
    return "NODEBUG";
}
#endif /* DEBUG */

/*
 * The PKTTAG tlv has additional bytes when firmware-signalling
 * mode has REUSESEQ flag set.
 */
#define BRCMF_FWS_TYPE_SEQ_LEN 2

/*
 * flags used to enable tlv signalling from firmware.
 */
#define BRCMF_FWS_FLAGS_RSSI_SIGNALS 0x0001
#define BRCMF_FWS_FLAGS_XONXOFF_SIGNALS 0x0002
#define BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS 0x0004
#define BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE 0x0008
#define BRCMF_FWS_FLAGS_PSQ_GENERATIONFSM_ENABLE 0x0010
#define BRCMF_FWS_FLAGS_PSQ_ZERO_BUFFER_ENABLE 0x0020
#define BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE 0x0040

#define BRCMF_FWS_MAC_DESC_TABLE_SIZE 32
#define BRCMF_FWS_MAC_DESC_ID_INVALID 0xff

#define BRCMF_FWS_HOSTIF_FLOWSTATE_OFF 0
#define BRCMF_FWS_HOSTIF_FLOWSTATE_ON 1
#define BRCMF_FWS_FLOWCONTROL_HIWATER 128
#define BRCMF_FWS_FLOWCONTROL_LOWATER 64

#define BRCMF_FWS_PSQ_PREC_COUNT ((BRCMF_FWS_FIFO_COUNT + 1) * 2)
#define BRCMF_FWS_PSQ_LEN 256

#define BRCMF_FWS_HTOD_FLAG_PKTFROMHOST 0x01
#define BRCMF_FWS_HTOD_FLAG_PKT_REQUESTED 0x02

enum brcmf_fws_should_schedule {BRCMF_FWS_NOSCHEDULE, BRCMF_FWS_SCHEDULE};

#define BRCMF_FWS_MODE_REUSESEQ_SHIFT 3 /* seq reuse */
#define BRCMF_FWS_MODE_SET_REUSESEQ(x, val)                \
    ((x) = ((x) & ~(1 << BRCMF_FWS_MODE_REUSESEQ_SHIFT)) | \
           (((val)&1) << BRCMF_FWS_MODE_REUSESEQ_SHIFT))
#define BRCMF_FWS_MODE_GET_REUSESEQ(x) (((x) >> BRCMF_FWS_MODE_REUSESEQ_SHIFT) & 1)

/**
 * enum brcmf_fws_skb_state - indicates processing state of netbuf.
 *
 * @BRCMF_FWS_SKBSTATE_NEW: brcmf_netbuf is newly arrived in the driver.
 * @BRCMF_FWS_SKBSTATE_DELAYED: brcmf_netbuf had to wait on queue.
 * @BRCMF_FWS_SKBSTATE_SUPPRESSED: brcmf_netbuf has been suppressed by firmware.
 * @BRCMF_FWS_SKBSTATE_TIM: allocated for TIM update info.
 */
enum brcmf_fws_skb_state {
    BRCMF_FWS_SKBSTATE_NEW,
    BRCMF_FWS_SKBSTATE_DELAYED,
    BRCMF_FWS_SKBSTATE_SUPPRESSED,
    BRCMF_FWS_SKBSTATE_TIM
};

/**
 * struct brcmf_netbuf_workspace - control buffer associated with skbuff.
 *
 * @bus_flags: 2 bytes reserved for bus specific parameters
 * @if_flags: holds interface index and packet related flags.
 * @htod: host to device packet identifier (used in PKTTAG tlv).
 * @htod_seq: this 16-bit is original seq number for every suppress packet.
 * @state: transmit state of the packet.
 * @mac: descriptor related to destination for this packet.
 *
 * This information is stored in control buffer struct brcmf_netbuf.workspace, which
 * provides 48 bytes of storage so this structure should not exceed that.
 */
struct brcmf_netbuf_workspace {
    uint16_t bus_flags;
    uint16_t if_flags;
    uint32_t htod;
    uint16_t htod_seq;
    enum brcmf_fws_skb_state state;
    struct brcmf_fws_mac_descriptor* mac;
    zx_status_t mac_status;
};

static_assert(sizeof(struct brcmf_netbuf_workspace) <= 48,
    "Struct brcmf_netbuf_workspace must be <= 48 bytes");

/*
 * macro casting skbuff control buffer to struct brcmf_netbuf_workspace.
 */
#define brcmf_workspace(netbuf) ((struct brcmf_netbuf_workspace*)((netbuf)->workspace))

// clang-format-off

/*
 * brcmf_netbuf control if flags
 *
 *  b[11]  - packet sent upon firmware request.
 *  b[10]  - packet only contains signalling data.
 *  b[9]   - packet is a tx packet.
 *  b[8]   - packet used requested credit
 *  b[7]   - interface in AP mode.
 *  b[3:0] - interface index.
 */
#define BRCMF_SKB_IF_FLAGS_REQUESTED_MASK    0x0800
#define BRCMF_SKB_IF_FLAGS_REQUESTED_SHIFT   11
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_MASK  0x0400
#define BRCMF_SKB_IF_FLAGS_SIGNAL_ONLY_SHIFT 10
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_MASK     0x0200
#define BRCMF_SKB_IF_FLAGS_TRANSMIT_SHIFT    9
#define BRCMF_SKB_IF_FLAGS_REQ_CREDIT_MASK   0x0100
#define BRCMF_SKB_IF_FLAGS_REQ_CREDIT_SHIFT  8
#define BRCMF_SKB_IF_FLAGS_IF_AP_MASK        0x0080
#define BRCMF_SKB_IF_FLAGS_IF_AP_SHIFT       7
#define BRCMF_SKB_IF_FLAGS_INDEX_MASK        0x000f
#define BRCMF_SKB_IF_FLAGS_INDEX_SHIFT       0

#define brcmf_skb_if_flags_set_field(netbuf, field, value)                               \
    brcmu_maskset16(&(brcmf_workspace(netbuf)->if_flags), BRCMF_SKB_IF_FLAGS_##field##_MASK, \
                    BRCMF_SKB_IF_FLAGS_##field##_SHIFT, (value))
#define brcmf_skb_if_flags_get_field(netbuf, field)                                   \
    brcmu_maskget16(brcmf_workspace(netbuf)->if_flags, BRCMF_SKB_IF_FLAGS_##field##_MASK, \
                    BRCMF_SKB_IF_FLAGS_##field##_SHIFT)

/*
 * brcmf_netbuf control packet identifier
 *
 * 32-bit packet identifier used in PKTTAG tlv from host to dongle.
 *
 * - Generated at the host (e.g. dhd)
 * - Seen as a generic sequence number by firmware except for the flags field.
 *
 * Generation   : b[31] => generation number for this packet [host->fw]
 *             OR, current generation number [fw->host]
 * Flags    : b[30:27] => command, status flags
 * FIFO-AC  : b[26:24] => AC-FIFO id
 * h-slot   : b[23:8] => hanger-slot
 * freerun  : b[7:0] => A free running counter
 */
#define BRCMF_SKB_HTOD_TAG_GENERATION_MASK  0x80000000
#define BRCMF_SKB_HTOD_TAG_GENERATION_SHIFT 31
#define BRCMF_SKB_HTOD_TAG_FLAGS_MASK       0x78000000
#define BRCMF_SKB_HTOD_TAG_FLAGS_SHIFT      27
#define BRCMF_SKB_HTOD_TAG_FIFO_MASK        0x07000000
#define BRCMF_SKB_HTOD_TAG_FIFO_SHIFT       24
#define BRCMF_SKB_HTOD_TAG_HSLOT_MASK       0x00ffff00
#define BRCMF_SKB_HTOD_TAG_HSLOT_SHIFT      8
#define BRCMF_SKB_HTOD_TAG_FREERUN_MASK     0x000000ff
#define BRCMF_SKB_HTOD_TAG_FREERUN_SHIFT    0

#define brcmf_skb_htod_tag_set_field(netbuf, field, value)                           \
    brcmu_maskset32(&(brcmf_workspace(netbuf)->htod), BRCMF_SKB_HTOD_TAG_##field##_MASK, \
                    BRCMF_SKB_HTOD_TAG_##field##_SHIFT, (value))
#define brcmf_skb_htod_tag_get_field(netbuf, field)                               \
    brcmu_maskget32(brcmf_workspace(netbuf)->htod, BRCMF_SKB_HTOD_TAG_##field##_MASK, \
                    BRCMF_SKB_HTOD_TAG_##field##_SHIFT)

#define BRCMF_SKB_HTOD_SEQ_FROMFW_MASK   0x2000
#define BRCMF_SKB_HTOD_SEQ_FROMFW_SHIFT  13
#define BRCMF_SKB_HTOD_SEQ_FROMDRV_MASK  0x1000
#define BRCMF_SKB_HTOD_SEQ_FROMDRV_SHIFT 12
#define BRCMF_SKB_HTOD_SEQ_NR_MASK       0x0fff
#define BRCMF_SKB_HTOD_SEQ_NR_SHIFT      0

#define brcmf_skb_htod_seq_set_field(netbuf, field, value)                               \
    brcmu_maskset16(&(brcmf_workspace(netbuf)->htod_seq), BRCMF_SKB_HTOD_SEQ_##field##_MASK, \
                    BRCMF_SKB_HTOD_SEQ_##field##_SHIFT, (value))
#define brcmf_skb_htod_seq_get_field(netbuf, field)                                   \
    brcmu_maskget16(brcmf_workspace(netbuf)->htod_seq, BRCMF_SKB_HTOD_SEQ_##field##_MASK, \
                    BRCMF_SKB_HTOD_SEQ_##field##_SHIFT)

#define BRCMF_FWS_TXSTAT_GENERATION_MASK  0x80000000
#define BRCMF_FWS_TXSTAT_GENERATION_SHIFT 31
#define BRCMF_FWS_TXSTAT_FLAGS_MASK       0x78000000
#define BRCMF_FWS_TXSTAT_FLAGS_SHIFT      27
#define BRCMF_FWS_TXSTAT_FIFO_MASK        0x07000000
#define BRCMF_FWS_TXSTAT_FIFO_SHIFT       24
#define BRCMF_FWS_TXSTAT_HSLOT_MASK       0x00FFFF00
#define BRCMF_FWS_TXSTAT_HSLOT_SHIFT      8
#define BRCMF_FWS_TXSTAT_FREERUN_MASK     0x000000FF
#define BRCMF_FWS_TXSTAT_FREERUN_SHIFT    0

// clang-format on

#define brcmf_txstatus_get_field(txs, field) \
    brcmu_maskget32(txs, BRCMF_FWS_TXSTAT_##field##_MASK, BRCMF_FWS_TXSTAT_##field##_SHIFT)

/* How long to defer borrowing in msec */
#define BRCMF_FWS_BORROW_DEFER_PERIOD_MSEC (100)

/**
 * enum brcmf_fws_fifo - fifo indices used by dongle firmware.
 *
 * @BRCMF_FWS_FIFO_FIRST: first fifo, ie. background.
 * @BRCMF_FWS_FIFO_AC_BK: fifo for background traffic.
 * @BRCMF_FWS_FIFO_AC_BE: fifo for best-effort traffic.
 * @BRCMF_FWS_FIFO_AC_VI: fifo for video traffic.
 * @BRCMF_FWS_FIFO_AC_VO: fifo for voice traffic.
 * @BRCMF_FWS_FIFO_BCMC: fifo for broadcast/multicast (AP only).
 * @BRCMF_FWS_FIFO_ATIM: fifo for ATIM (AP only).
 * @BRCMF_FWS_FIFO_COUNT: number of fifos.
 */
enum brcmf_fws_fifo {
    BRCMF_FWS_FIFO_FIRST,
    BRCMF_FWS_FIFO_AC_BK = BRCMF_FWS_FIFO_FIRST,
    BRCMF_FWS_FIFO_AC_BE,
    BRCMF_FWS_FIFO_AC_VI,
    BRCMF_FWS_FIFO_AC_VO,
    BRCMF_FWS_FIFO_BCMC,
    BRCMF_FWS_FIFO_ATIM,
    BRCMF_FWS_FIFO_COUNT
};

/**
 * enum brcmf_fws_txstatus - txstatus flag values.
 *
 * @BRCMF_FWS_TXSTATUS_DISCARD:
 *  host is free to discard the packet.
 * @BRCMF_FWS_TXSTATUS_CORE_SUPPRESS:
 *  802.11 core suppressed the packet.
 * @BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS:
 *  firmware suppress the packet as device is already in PS mode.
 * @BRCMF_FWS_TXSTATUS_FW_TOSSED:
 *  firmware tossed the packet.
 * @BRCMF_FWS_TXSTATUS_HOST_TOSSED:
 *  host tossed the packet.
 */
enum brcmf_fws_txstatus {
    BRCMF_FWS_TXSTATUS_DISCARD,
    BRCMF_FWS_TXSTATUS_CORE_SUPPRESS,
    BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS,
    BRCMF_FWS_TXSTATUS_FW_TOSSED,
    BRCMF_FWS_TXSTATUS_HOST_TOSSED
};

enum brcmf_fws_fcmode {
    BRCMF_FWS_FCMODE_NONE,
    BRCMF_FWS_FCMODE_IMPLIED_CREDIT,
    BRCMF_FWS_FCMODE_EXPLICIT_CREDIT
};

enum brcmf_fws_mac_desc_state { BRCMF_FWS_STATE_OPEN = 1, BRCMF_FWS_STATE_CLOSE };

/**
 * struct brcmf_fws_mac_descriptor - firmware signalling data per node/interface
 *
 * @occupied: slot is in use.
 * @mac_handle: handle for mac entry determined by firmware.
 * @interface_id: interface index.
 * @state: current state.
 * @suppressed: mac entry is suppressed.
 * @generation: generation bit.
 * @ac_bitmap: ac queue bitmap.
 * @requested_credit: credits requested by firmware.
 * @ea: ethernet address.
 * @seq: per-node free-running sequence.
 * @psq: power-save queue.
 * @transit_count: packet in transit to firmware.
 */
struct brcmf_fws_mac_descriptor {
    char name[16];
    uint8_t occupied;
    uint8_t mac_handle;
    uint8_t interface_id;
    uint8_t state;
    bool suppressed;
    uint8_t generation;
    uint8_t ac_bitmap;
    uint8_t requested_credit;
    uint8_t requested_packet;
    uint8_t ea[ETH_ALEN];
    uint8_t seq[BRCMF_FWS_FIFO_COUNT];
    struct pktq psq;
    int transit_count;
    int suppr_transit_count;
    bool send_tim_signal;
    uint8_t traffic_pending_bmp;
    uint8_t traffic_lastreported_bmp;
};

#define BRCMF_FWS_HANGER_MAXITEMS 1024

/**
 * enum brcmf_fws_hanger_item_state - state of hanger item.
 *
 * @BRCMF_FWS_HANGER_ITEM_STATE_FREE: item is free for use.
 * @BRCMF_FWS_HANGER_ITEM_STATE_INUSE: item is in use.
 * @BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED: item was suppressed.
 */
enum brcmf_fws_hanger_item_state {
    BRCMF_FWS_HANGER_ITEM_STATE_FREE = 1,
    BRCMF_FWS_HANGER_ITEM_STATE_INUSE,
    BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED
};

/**
 * struct brcmf_fws_hanger_item - single entry for tx pending packet.
 *
 * @state: entry is either free or occupied.
 * @pkt: packet itself.
 */
struct brcmf_fws_hanger_item {
    enum brcmf_fws_hanger_item_state state;
    struct brcmf_netbuf* pkt;
};

/**
 * struct brcmf_fws_hanger - holds packets awaiting firmware txstatus.
 *
 * @pushed: packets pushed to await txstatus.
 * @popped: packets popped upon handling txstatus.
 * @failed_to_push: packets that could not be pushed.
 * @failed_to_pop: packets that could not be popped.
 * @failed_slotfind: packets for which failed to find an entry.
 * @slot_pos: last returned item index for a free entry.
 * @items: array of hanger items.
 */
struct brcmf_fws_hanger {
    uint32_t pushed;
    uint32_t popped;
    uint32_t failed_to_push;
    uint32_t failed_to_pop;
    uint32_t failed_slotfind;
    uint32_t slot_pos;
    struct brcmf_fws_hanger_item items[BRCMF_FWS_HANGER_MAXITEMS];
};

struct brcmf_fws_macdesc_table {
    struct brcmf_fws_mac_descriptor nodes[BRCMF_FWS_MAC_DESC_TABLE_SIZE];
    struct brcmf_fws_mac_descriptor iface[BRCMF_MAX_IFS];
    struct brcmf_fws_mac_descriptor other;
};

struct brcmf_fws_stats {
    uint32_t tlv_parse_failed;
    uint32_t tlv_invalid_type;
    uint32_t header_only_pkt;
    uint32_t header_pulls;
    uint32_t pkt2bus;
    uint32_t send_pkts[5];
    uint32_t requested_sent[5];
    uint32_t generic_error;
    uint32_t mac_update_failed;
    uint32_t mac_ps_update_failed;
    uint32_t if_update_failed;
    uint32_t packet_request_failed;
    uint32_t credit_request_failed;
    uint32_t rollback_success;
    uint32_t rollback_failed;
    uint32_t delayq_full_error;
    uint32_t supprq_full_error;
    uint32_t txs_indicate;
    uint32_t txs_discard;
    uint32_t txs_supp_core;
    uint32_t txs_supp_ps;
    uint32_t txs_tossed;
    uint32_t txs_host_tossed;
    uint32_t bus_flow_block;
    uint32_t fws_flow_block;
};

struct brcmf_fws_info {
    struct brcmf_pub* drvr;
    //spinlock_t spinlock;
    struct brcmf_fws_stats stats;
    struct brcmf_fws_hanger hanger;
    enum brcmf_fws_fcmode fcmode;
    bool fw_signals;
    bool bcmc_credit_check;
    struct brcmf_fws_macdesc_table desc;
    struct workqueue_struct* fws_wq;
    struct work_struct fws_dequeue_work;
    uint32_t fifo_enqpkt[BRCMF_FWS_FIFO_COUNT];
    int fifo_credit[BRCMF_FWS_FIFO_COUNT];
    int credits_borrowed[BRCMF_FWS_FIFO_AC_VO + 1];
    int deq_node_pos[BRCMF_FWS_FIFO_COUNT];
    uint32_t fifo_credit_map;
    uint32_t fifo_delay_map;
    zx_time_t borrow_defer_timestamp;
    bool bus_flow_blocked;
    bool creditmap_received;
    uint8_t mode;
    bool avoid_queueing;
};

/*
 * brcmf_fws_prio2fifo - mapping from 802.1d priority to firmware fifo index.
 */
static const int brcmf_fws_prio2fifo[] = {
    BRCMF_FWS_FIFO_AC_BE, BRCMF_FWS_FIFO_AC_BK, BRCMF_FWS_FIFO_AC_BK, BRCMF_FWS_FIFO_AC_BE,
    BRCMF_FWS_FIFO_AC_VI, BRCMF_FWS_FIFO_AC_VI, BRCMF_FWS_FIFO_AC_VO, BRCMF_FWS_FIFO_AC_VO
};

#define BRCMF_FWS_TLV_DEF(name, id, len) \
    case BRCMF_FWS_TYPE_##name:          \
        *len_out = len;                  \
        return ZX_OK;

/**
 * brcmf_fws_get_tlv_len() - returns defined length for given tlv id.
 *
 * @fws: firmware-signalling information.
 * @id: identifier of the TLV.
 * @len_out: set to the specified length for the given TLV if return is ZX_OK; otherwise 0.
 *
 * Return: ZX_OK if TLV is found; otherwise error.
 */
static zx_status_t brcmf_fws_get_tlv_len(struct brcmf_fws_info* fws, enum brcmf_fws_tlv_type id,
                                         int* len_out) {
    if (!len_out) {
        return ZX_ERR_INVALID_ARGS;
    }
    switch (id) {
        BRCMF_FWS_TLV_DEFLIST
    default:
        fws->stats.tlv_invalid_type++;
        break;
    }
    *len_out = 0;
    return ZX_ERR_INVALID_ARGS;
}
#undef BRCMF_FWS_TLV_DEF

static void brcmf_fws_lock(struct brcmf_fws_info* fws) { // __TA_ACQUIRE(&fws->spinlock) {
    //spin_lock_irqsave(&fws->spinlock, fws->flags);
    pthread_mutex_lock(&irq_callback_lock);
}

static void brcmf_fws_unlock(struct brcmf_fws_info* fws) { // __TA_RELEASE(&fws->spinlock) {
    //spin_unlock_irqrestore(&fws->spinlock, fws->flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

static bool brcmf_fws_ifidx_match(struct brcmf_netbuf* netbuf, void* arg) {
    uint32_t ifidx = brcmf_skb_if_flags_get_field(netbuf, INDEX);
    return ifidx == *(uint32_t*)arg;
}

static void brcmf_fws_psq_flush(struct brcmf_fws_info* fws, struct pktq* q, int ifidx) {
    bool (*matchfn)(struct brcmf_netbuf*, void*) = NULL;
    struct brcmf_netbuf* netbuf;
    int prec;

    if (ifidx != -1) {
        matchfn = brcmf_fws_ifidx_match;
    }
    for (prec = 0; prec < q->num_prec; prec++) {
        netbuf = brcmu_pktq_pdeq_match(q, prec, matchfn, &ifidx);
        while (netbuf) {
            brcmu_pkt_buf_free_skb(netbuf);
            netbuf = brcmu_pktq_pdeq_match(q, prec, matchfn, &ifidx);
        }
    }
}

static void brcmf_fws_hanger_init(struct brcmf_fws_hanger* hanger) {
    int i;

    memset(hanger, 0, sizeof(*hanger));
    for (i = 0; i < (int)ARRAY_SIZE(hanger->items); i++) {
        hanger->items[i].state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
    }
}

static uint32_t brcmf_fws_hanger_get_free_slot(struct brcmf_fws_hanger* h) {
    uint32_t i;

    i = (h->slot_pos + 1) % BRCMF_FWS_HANGER_MAXITEMS;

    while (i != h->slot_pos) {
        if (h->items[i].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
            h->slot_pos = i;
            goto done;
        }
        i++;
        if (i == BRCMF_FWS_HANGER_MAXITEMS) {
            i = 0;
        }
    }
    brcmf_err("all slots occupied\n");
    h->failed_slotfind++;
    i = BRCMF_FWS_HANGER_MAXITEMS;
done:
    return i;
}

static zx_status_t brcmf_fws_hanger_pushpkt(struct brcmf_fws_hanger* h, struct brcmf_netbuf* pkt,
                                            uint32_t slot_id) {
    if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (h->items[slot_id].state != BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
        brcmf_err("slot is not free\n");
        h->failed_to_push++;
        return ZX_ERR_BAD_STATE;
    }

    h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_INUSE;
    h->items[slot_id].pkt = pkt;
    h->pushed++;
    return ZX_OK;
}

static inline zx_status_t brcmf_fws_hanger_poppkt(struct brcmf_fws_hanger* h, uint32_t slot_id,
                                                  struct brcmf_netbuf** pktout, bool remove_item) {
    if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (h->items[slot_id].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
        brcmf_err("entry not in use\n");
        h->failed_to_pop++;
        return ZX_ERR_BAD_STATE;
    }

    *pktout = h->items[slot_id].pkt;
    if (remove_item) {
        h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
        h->items[slot_id].pkt = NULL;
        h->popped++;
    }
    return ZX_OK;
}

static zx_status_t brcmf_fws_hanger_mark_suppressed(struct brcmf_fws_hanger* h, uint32_t slot_id) {
    if (slot_id >= BRCMF_FWS_HANGER_MAXITEMS) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (h->items[slot_id].state == BRCMF_FWS_HANGER_ITEM_STATE_FREE) {
        brcmf_err("entry not in use\n");
        return ZX_ERR_BAD_STATE;
    }

    h->items[slot_id].state = BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED;
    return ZX_OK;
}

static void brcmf_fws_hanger_cleanup(struct brcmf_fws_info* fws,
                                     bool (*fn)(struct brcmf_netbuf*, void*), int ifidx) {
    struct brcmf_fws_hanger* h = &fws->hanger;
    struct brcmf_netbuf* netbuf;
    int i;
    enum brcmf_fws_hanger_item_state s;

    for (i = 0; i < (int)ARRAY_SIZE(h->items); i++) {
        s = h->items[i].state;
        if (s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE ||
                s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE_SUPPRESSED) {
            netbuf = h->items[i].pkt;
            if (fn == NULL || fn(netbuf, &ifidx)) {
                /* suppress packets freed from psq */
                if (s == BRCMF_FWS_HANGER_ITEM_STATE_INUSE) {
                    brcmu_pkt_buf_free_skb(netbuf);
                }
                h->items[i].state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
            }
        }
    }
}

static void brcmf_fws_macdesc_set_name(struct brcmf_fws_info* fws,
                                       struct brcmf_fws_mac_descriptor* desc) {
    if (desc == &fws->desc.other) {
        strlcpy(desc->name, "MAC-OTHER", sizeof(desc->name));
    } else if (desc->mac_handle)
        snprintf(desc->name, sizeof(desc->name), "MAC-%d:%d", desc->mac_handle,
                  desc->interface_id);
    else {
        snprintf(desc->name, sizeof(desc->name), "MACIF:%d", desc->interface_id);
    }
}

static void brcmf_fws_macdesc_init(struct brcmf_fws_mac_descriptor* desc, uint8_t* addr,
                                   uint8_t ifidx) {
    brcmf_dbg(TRACE, "enter: desc %p ea=%pM, ifidx=%u\n", desc, addr, ifidx);
    desc->occupied = 1;
    desc->state = BRCMF_FWS_STATE_OPEN;
    desc->requested_credit = 0;
    desc->requested_packet = 0;
    /* depending on use may need ifp->bsscfgidx instead */
    desc->interface_id = ifidx;
    desc->ac_bitmap = 0xff; /* update this when handling APSD */
    if (addr) {
        memcpy(&desc->ea[0], addr, ETH_ALEN);
    }
}

static void brcmf_fws_macdesc_deinit(struct brcmf_fws_mac_descriptor* desc) {
    brcmf_dbg(TRACE, "enter: ea=%pM, ifidx=%u\n", desc->ea, desc->interface_id);
    desc->occupied = 0;
    desc->state = BRCMF_FWS_STATE_CLOSE;
    desc->requested_credit = 0;
    desc->requested_packet = 0;
}

static zx_status_t brcmf_fws_macdesc_lookup(struct brcmf_fws_info* fws, uint8_t* ea,
                                            struct brcmf_fws_mac_descriptor** macdesc_out) {
    struct brcmf_fws_mac_descriptor* entry;
    int i;

    if (macdesc_out) {
        *macdesc_out = NULL;
    }

    if (ea == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    entry = &fws->desc.nodes[0];
    for (i = 0; i < (int)ARRAY_SIZE(fws->desc.nodes); i++) {
        if (entry->occupied && !memcmp(entry->ea, ea, ETH_ALEN)) {
            if (macdesc_out) {
                *macdesc_out = entry;
            }
            return ZX_OK;
        }
        entry++;
    }

    return ZX_ERR_NOT_FOUND;
}

static zx_status_t brcmf_fws_macdesc_find(struct brcmf_fws_info* fws,
                                          struct brcmf_if* ifp, uint8_t* da,
                                          struct brcmf_fws_mac_descriptor** macdesc_out) {
    *macdesc_out = &fws->desc.other; // TODO(cphoenix): Isn't this overwritten?
    bool multicast;
    zx_status_t ret;

    multicast = address_is_multicast(da);

    /* Multicast destination, STA and P2P clients get the interface entry.
     * STA/GC gets the Mac Entry for TDLS destinations, TDLS destinations
     * have their own entry.
     */
    if (multicast && ifp->fws_desc) {
        *macdesc_out = ifp->fws_desc;
        ret = ZX_OK;
        goto done;
    }

    ret = brcmf_fws_macdesc_lookup(fws, da, macdesc_out);
    if (ret != ZX_OK) {
        *macdesc_out = ifp->fws_desc;
        // TODO(cphoenix): Check logic and expectations here, e.g. is fws_desc non-NULL?
        ret = ZX_OK;
    }

done:
    return ret;
}

static bool brcmf_fws_macdesc_closed(struct brcmf_fws_info* fws,
                                     struct brcmf_fws_mac_descriptor* entry, int fifo) {
    struct brcmf_fws_mac_descriptor* if_entry;
    bool closed;

    /* for unique destination entries the related interface
     * may be closed.
     */
    if (entry->mac_handle) {
        if_entry = &fws->desc.iface[entry->interface_id];
        if (if_entry->state == BRCMF_FWS_STATE_CLOSE) {
            return true;
        }
    }
    /* an entry is closed when the state is closed and
     * the firmware did not request anything.
     */
    closed = entry->state == BRCMF_FWS_STATE_CLOSE && !entry->requested_credit &&
             !entry->requested_packet;

    /* Or firmware does not allow traffic for given fifo */
    return closed || !(entry->ac_bitmap & BIT(fifo));
}

static void brcmf_fws_macdesc_cleanup(struct brcmf_fws_info* fws,
                                      struct brcmf_fws_mac_descriptor* entry, int ifidx) {
    if (entry->occupied && (ifidx == -1 || ifidx == entry->interface_id)) {
        brcmf_fws_psq_flush(fws, &entry->psq, ifidx);
        entry->occupied = !!(entry->psq.len);
    }
}

static void brcmf_fws_bus_txq_cleanup(struct brcmf_fws_info* fws,
                                      bool (*fn)(struct brcmf_netbuf*, void*), int ifidx) {
    struct brcmf_fws_hanger_item* hi;
    struct pktq* txq;
    struct brcmf_netbuf* netbuf;
    int prec;
    uint32_t hslot;
    zx_status_t err;

    err = brcmf_bus_gettxq(fws->drvr->bus_if, &txq);
    if (err != ZX_OK) {
        brcmf_dbg(TRACE, "no txq to clean up\n");
        return;
    }

    for (prec = 0; prec < txq->num_prec; prec++) {
        netbuf = brcmu_pktq_pdeq_match(txq, prec, fn, &ifidx);
        while (netbuf) {
            hslot = brcmf_skb_htod_tag_get_field(netbuf, HSLOT);
            hi = &fws->hanger.items[hslot];
            WARN_ON(netbuf != hi->pkt);
            hi->state = BRCMF_FWS_HANGER_ITEM_STATE_FREE;
            brcmu_pkt_buf_free_skb(netbuf);
            netbuf = brcmu_pktq_pdeq_match(txq, prec, fn, &ifidx);
        }
    }
}

static void brcmf_fws_cleanup(struct brcmf_fws_info* fws, int ifidx) {
    int i;
    struct brcmf_fws_mac_descriptor* table;
    bool (*matchfn)(struct brcmf_netbuf*, void*) = NULL;

    if (fws == NULL) {
        return;
    }

    if (ifidx != -1) {
        matchfn = brcmf_fws_ifidx_match;
    }

    /* cleanup individual nodes */
    table = &fws->desc.nodes[0];
    for (i = 0; i < (int)ARRAY_SIZE(fws->desc.nodes); i++) {
        brcmf_fws_macdesc_cleanup(fws, &table[i], ifidx);
    }

    brcmf_fws_macdesc_cleanup(fws, &fws->desc.other, ifidx);
    brcmf_fws_bus_txq_cleanup(fws, matchfn, ifidx);
    brcmf_fws_hanger_cleanup(fws, matchfn, ifidx);
}

static uint8_t brcmf_fws_hdrpush(struct brcmf_fws_info* fws, struct brcmf_netbuf* netbuf) {
    struct brcmf_fws_mac_descriptor* entry = brcmf_workspace(netbuf)->mac;
    uint8_t* wlh;
    uint16_t data_offset = 0;
    uint8_t fillers;
    uint32_t pkttag = brcmf_workspace(netbuf)->htod;
    uint16_t pktseq = brcmf_workspace(netbuf)->htod_seq;

    brcmf_dbg(TRACE, "enter: %s, idx=%d hslot=%d htod %X seq %X\n", entry->name,
              brcmf_skb_if_flags_get_field(netbuf, INDEX), (pkttag) >> 8 & 0xffff,
              brcmf_workspace(netbuf)->htod, brcmf_workspace(netbuf)->htod_seq);
    if (entry->send_tim_signal) {
        data_offset += 2 + BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN;
    }
    if (BRCMF_FWS_MODE_GET_REUSESEQ(fws->mode)) {
        data_offset += BRCMF_FWS_TYPE_SEQ_LEN;
    }
    /* +2 is for Type[1] and Len[1] in TLV, plus TIM signal */
    data_offset += 2 + BRCMF_FWS_TYPE_PKTTAG_LEN;
    fillers = round_up(data_offset, 4) - data_offset;
    data_offset += fillers;

    brcmf_netbuf_grow_head(netbuf, data_offset);
    wlh = netbuf->data;

    wlh[0] = BRCMF_FWS_TYPE_PKTTAG;
    wlh[1] = BRCMF_FWS_TYPE_PKTTAG_LEN;
    memcpy(&wlh[2], &pkttag, sizeof(pkttag));
    if (BRCMF_FWS_MODE_GET_REUSESEQ(fws->mode)) {
        wlh[1] += BRCMF_FWS_TYPE_SEQ_LEN;
        memcpy(&wlh[2 + BRCMF_FWS_TYPE_PKTTAG_LEN], &pktseq, sizeof(pktseq));
    }
    wlh += wlh[1] + 2;

    if (entry->send_tim_signal) {
        entry->send_tim_signal = 0;
        wlh[0] = BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP;
        wlh[1] = BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN;
        wlh[2] = entry->mac_handle;
        wlh[3] = entry->traffic_pending_bmp;
        brcmf_dbg(TRACE, "adding TIM info: handle %d bmp 0x%X\n", entry->mac_handle,
                  entry->traffic_pending_bmp);
        wlh += BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN + 2;
        entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
    }
    if (fillers) {
        memset(wlh, BRCMF_FWS_TYPE_FILLER, fillers);
    }

    return (uint8_t)(data_offset >> 2);
}

static bool brcmf_fws_tim_update(struct brcmf_fws_info* fws, struct brcmf_fws_mac_descriptor* entry,
                                 int fifo, bool send_immediately) {
    struct brcmf_netbuf* netbuf;
    struct brcmf_netbuf_workspace* workspace;
    zx_status_t err;
    uint32_t len;
    uint8_t data_offset;
    int ifidx;

    /* check delayedQ and suppressQ in one call using bitmap */
    if (brcmu_pktq_mlen(&entry->psq, 3 << (fifo * 2)) == 0) {
        entry->traffic_pending_bmp &= ~NBITVAL(fifo);
    } else {
        entry->traffic_pending_bmp |= NBITVAL(fifo);
    }

    entry->send_tim_signal = false;
    if (entry->traffic_lastreported_bmp != entry->traffic_pending_bmp) {
        entry->send_tim_signal = true;
    }
    if (send_immediately && entry->send_tim_signal && entry->state == BRCMF_FWS_STATE_CLOSE) {
        /* create a dummy packet and sent that. The traffic          */
        /* bitmap info will automatically be attached to that packet */
        len = BRCMF_FWS_TYPE_PKTTAG_LEN + 2 + BRCMF_FWS_TYPE_SEQ_LEN +
              BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP_LEN + 2 + 4 + fws->drvr->hdrlen;
        netbuf = brcmu_pkt_buf_get_skb(len);
        if (netbuf == NULL) {
            return false;
        }
        brcmf_netbuf_shrink_head(netbuf, len);
        workspace = brcmf_workspace(netbuf);
        workspace->mac = entry;
        workspace->mac_status = ZX_OK;
        workspace->state = BRCMF_FWS_SKBSTATE_TIM;
        workspace->htod = 0;
        workspace->htod_seq = 0;
        data_offset = brcmf_fws_hdrpush(fws, netbuf);
        ifidx = brcmf_skb_if_flags_get_field(netbuf, INDEX);
        brcmf_fws_unlock(fws);
        err = brcmf_proto_txdata(fws->drvr, ifidx, data_offset, netbuf);
        brcmf_fws_lock(fws);
        if (err != ZX_OK) {
            brcmu_pkt_buf_free_skb(netbuf);
        }
        return true;
    }
    return false;
}

static void brcmf_fws_flow_control_check(struct brcmf_fws_info* fws, struct pktq* pq,
                                         uint8_t if_id) {
    struct brcmf_if* ifp = brcmf_get_ifp(fws->drvr, if_id);

    if (WARN_ON(!ifp)) {
        return;
    }

    if ((ifp->netif_stop & BRCMF_NETIF_STOP_REASON_FWS_FC) &&
            pq->len <= BRCMF_FWS_FLOWCONTROL_LOWATER) {
        brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_FWS_FC, false);
    }
    if (!(ifp->netif_stop & BRCMF_NETIF_STOP_REASON_FWS_FC) &&
            pq->len >= BRCMF_FWS_FLOWCONTROL_HIWATER) {
        fws->stats.fws_flow_block++;
        brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_FWS_FC, true);
    }
    return;
}

static zx_status_t brcmf_fws_rssi_indicate(struct brcmf_fws_info* fws, int8_t rssi) {
    brcmf_dbg(CTL, "rssi %d\n", rssi);
    return ZX_OK;
}

static int brcmf_fws_macdesc_indicate(struct brcmf_fws_info* fws, uint8_t type, uint8_t* data) {
    struct brcmf_fws_mac_descriptor* entry;
    struct brcmf_fws_mac_descriptor* existing;
    uint8_t mac_handle;
    uint8_t ifidx;
    uint8_t* addr;
    zx_status_t ret;

    mac_handle = *data++;
    ifidx = *data++;
    addr = data;

    entry = &fws->desc.nodes[mac_handle & 0x1F];
    if (type == BRCMF_FWS_TYPE_MACDESC_DEL) {
        if (entry->occupied) {
            brcmf_dbg(TRACE, "deleting %s mac %pM\n", entry->name, addr);
            brcmf_fws_lock(fws);
            brcmf_fws_macdesc_cleanup(fws, entry, -1);
            brcmf_fws_macdesc_deinit(entry);
            brcmf_fws_unlock(fws);
        } else {
            fws->stats.mac_update_failed++;
        }
        return ZX_OK;
    }

    ret = brcmf_fws_macdesc_lookup(fws, addr, &existing);
    if (ret != ZX_OK) {
        if (!entry->occupied) {
            brcmf_fws_lock(fws);
            entry->mac_handle = mac_handle;
            brcmf_fws_macdesc_init(entry, addr, ifidx);
            brcmf_fws_macdesc_set_name(fws, entry);
            brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT, BRCMF_FWS_PSQ_LEN);
            brcmf_fws_unlock(fws);
            brcmf_dbg(TRACE, "add %s mac %pM\n", entry->name, addr);
        } else {
            fws->stats.mac_update_failed++;
        }
    } else {
        if (entry != existing) {
            brcmf_dbg(TRACE, "copy mac %s\n", existing->name);
            brcmf_fws_lock(fws);
            memcpy(entry, existing, offsetof(struct brcmf_fws_mac_descriptor, psq));
            entry->mac_handle = mac_handle;
            brcmf_fws_macdesc_deinit(existing);
            brcmf_fws_macdesc_set_name(fws, entry);
            brcmf_fws_unlock(fws);
            brcmf_dbg(TRACE, "relocate %s mac %pM\n", entry->name, addr);
        } else {
            brcmf_dbg(TRACE, "use existing\n");
            WARN_ON(entry->mac_handle != mac_handle);
            /* TODO: what should we do here: continue, reinit, .. */
        }
    }
    return ZX_OK;
}

static enum brcmf_fws_should_schedule brcmf_fws_macdesc_state_indicate(struct brcmf_fws_info* fws,
                                                                       uint8_t type,
                                                                       uint8_t* data) {
    struct brcmf_fws_mac_descriptor* entry;
    uint8_t mac_handle;
    int ret;

    mac_handle = data[0];
    entry = &fws->desc.nodes[mac_handle & 0x1F];
    if (!entry->occupied) {
        fws->stats.mac_ps_update_failed++;
        brcmf_err("Unoccupied entry\n");
        return BRCMF_FWS_NOSCHEDULE;
    }
    brcmf_fws_lock(fws);
    /* a state update should wipe old credits */
    entry->requested_credit = 0;
    entry->requested_packet = 0;
    if (type == BRCMF_FWS_TYPE_MAC_OPEN) {
        entry->state = BRCMF_FWS_STATE_OPEN;
        ret = BRCMF_FWS_SCHEDULE;
    } else {
        entry->state = BRCMF_FWS_STATE_CLOSE;
        brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_BK, false);
        brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_BE, false);
        brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_VI, false);
        brcmf_fws_tim_update(fws, entry, BRCMF_FWS_FIFO_AC_VO, true);
        ret = BRCMF_FWS_NOSCHEDULE;
    }
    brcmf_fws_unlock(fws);
    return ret;
}

static enum brcmf_fws_should_schedule brcmf_fws_interface_state_indicate(struct brcmf_fws_info* fws,
                                                                         uint8_t type,
                                                                         uint8_t* data) {
    struct brcmf_fws_mac_descriptor* entry;
    uint8_t ifidx;
    enum brcmf_fws_should_schedule ret;

    ifidx = data[0];

    if (ifidx >= BRCMF_MAX_IFS) {
        brcmf_err("ifidx %d bigger than BRCMF_MAX_IFS\n", ifidx);
        ret = BRCMF_FWS_NOSCHEDULE;
        goto fail;
    }

    entry = &fws->desc.iface[ifidx];
    if (!entry->occupied) {
        brcmf_err("Entry %d unoccupied\n", ifidx);
        ret = BRCMF_FWS_NOSCHEDULE;
        goto fail;
    }

    brcmf_dbg(TRACE, "%s (%d): %s\n", brcmf_fws_get_tlv_name(type), type, entry->name);
    brcmf_fws_lock(fws);
    switch (type) {
    case BRCMF_FWS_TYPE_INTERFACE_OPEN:
        entry->state = BRCMF_FWS_STATE_OPEN;
        ret = BRCMF_FWS_SCHEDULE;
        break;
    case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
        entry->state = BRCMF_FWS_STATE_CLOSE;
        ret = BRCMF_FWS_NOSCHEDULE;
        break;
    default:
        brcmf_err("Invalid type %d\n", type);
        ret = BRCMF_FWS_NOSCHEDULE;
        brcmf_fws_unlock(fws);
        goto fail;
    }
    brcmf_fws_unlock(fws);
    return ret;

fail:
    fws->stats.if_update_failed++;
    return ret;
}

static enum brcmf_fws_should_schedule brcmf_fws_request_indicate(struct brcmf_fws_info* fws,
                                                                 uint8_t type, uint8_t* data) {
    struct brcmf_fws_mac_descriptor* entry;

    entry = &fws->desc.nodes[data[1] & 0x1F];
    if (!entry->occupied) {
        if (type == BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT) {
            fws->stats.credit_request_failed++;
        } else {
            fws->stats.packet_request_failed++;
        }
        brcmf_err("Unoccupied entry %d\n", data[1] & 0x1F);
        return BRCMF_FWS_NOSCHEDULE;
    }

    brcmf_dbg(TRACE, "%s (%d): %s cnt %d bmp %d\n", brcmf_fws_get_tlv_name(type), type, entry->name,
              data[0], data[2]);
    brcmf_fws_lock(fws);
    if (type == BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT) {
        entry->requested_credit = data[0];
    } else {
        entry->requested_packet = data[0];
    }

    entry->ac_bitmap = data[2];
    brcmf_fws_unlock(fws);
    return BRCMF_FWS_SCHEDULE;
}

static void brcmf_fws_macdesc_use_req_credit(struct brcmf_fws_mac_descriptor* entry,
                                             struct brcmf_netbuf* netbuf) {
    if (entry->requested_credit > 0) {
        entry->requested_credit--;
        brcmf_skb_if_flags_set_field(netbuf, REQUESTED, 1);
        brcmf_skb_if_flags_set_field(netbuf, REQ_CREDIT, 1);
        if (entry->state != BRCMF_FWS_STATE_CLOSE) {
            brcmf_err("requested credit set while mac not closed!\n");
        }
    } else if (entry->requested_packet > 0) {
        entry->requested_packet--;
        brcmf_skb_if_flags_set_field(netbuf, REQUESTED, 1);
        brcmf_skb_if_flags_set_field(netbuf, REQ_CREDIT, 0);
        if (entry->state != BRCMF_FWS_STATE_CLOSE) {
            brcmf_err("requested packet set while mac not closed!\n");
        }
    } else {
        brcmf_skb_if_flags_set_field(netbuf, REQUESTED, 0);
        brcmf_skb_if_flags_set_field(netbuf, REQ_CREDIT, 0);
    }
}

static void brcmf_fws_macdesc_return_req_credit(struct brcmf_netbuf* netbuf) {
    struct brcmf_fws_mac_descriptor* entry = brcmf_workspace(netbuf)->mac;

    if ((brcmf_skb_if_flags_get_field(netbuf, REQ_CREDIT)) &&
            (entry->state == BRCMF_FWS_STATE_CLOSE)) {
        entry->requested_credit++;
    }
}

static void brcmf_fws_return_credits(struct brcmf_fws_info* fws, uint8_t fifo, uint8_t credits) {
    int lender_ac;
    int* borrowed;
    int* fifo_credit;

    if (!credits) {
        return;
    }

    fws->fifo_credit_map |= 1 << fifo;

    if ((fifo == BRCMF_FWS_FIFO_AC_BE) && (fws->credits_borrowed[0])) {
        for (lender_ac = BRCMF_FWS_FIFO_AC_VO; lender_ac >= 0; lender_ac--) {
            borrowed = &fws->credits_borrowed[lender_ac];
            if (*borrowed) {
                fws->fifo_credit_map |= (1 << lender_ac);
                fifo_credit = &fws->fifo_credit[lender_ac];
                if (*borrowed >= credits) {
                    *borrowed -= credits;
                    *fifo_credit += credits;
                    return;
                } else {
                    credits -= *borrowed;
                    *fifo_credit += *borrowed;
                    *borrowed = 0;
                }
            }
        }
    }

    fws->fifo_credit[fifo] += credits;
}

static void brcmf_fws_schedule_deq(struct brcmf_fws_info* fws) {
    /* only schedule dequeue when there are credits for delayed traffic */
    if ((fws->fifo_credit_map & fws->fifo_delay_map) ||
            (!brcmf_fws_fc_active(fws) && fws->fifo_delay_map)) {
        workqueue_schedule(fws->fws_wq, &fws->fws_dequeue_work);
    }
}

static zx_status_t brcmf_fws_enq(struct brcmf_fws_info* fws, enum brcmf_fws_skb_state state,
                                 int fifo, struct brcmf_netbuf* p) {
    int prec = 2 * fifo;
    uint32_t* qfull_stat = &fws->stats.delayq_full_error;
    struct brcmf_fws_mac_descriptor* entry;
    struct pktq* pq;
    struct brcmf_netbuf_list* queue;
    struct brcmf_netbuf* p_head;
    struct brcmf_netbuf* p_tail;
    uint32_t fr_new;
    uint32_t fr_compare;

    entry = brcmf_workspace(p)->mac;
    if (entry == NULL) {
        brcmf_err("no mac descriptor found for netbuf %p\n", p);
        return ZX_ERR_NOT_FOUND;
    }

    brcmf_dbg(DATA, "enter: fifo %d netbuf %p\n", fifo, p);
    if (state == BRCMF_FWS_SKBSTATE_SUPPRESSED) {
        prec += 1;
        qfull_stat = &fws->stats.supprq_full_error;

        /* Fix out of order delivery of frames. Dont assume frame    */
        /* can be inserted at the end, but look for correct position */
        pq = &entry->psq;
        if (pktq_full(pq) || pktq_pfull(pq, prec)) {
            *qfull_stat += 1;
            return ZX_ERR_NO_RESOURCES;
        }
        queue = &pq->q[prec].skblist;

        p_head = brcmf_netbuf_list_peek_head(queue);
        p_tail = brcmf_netbuf_list_peek_tail(queue);
        fr_new = brcmf_skb_htod_tag_get_field(p, FREERUN);

        while (p_head != p_tail) {
            fr_compare = brcmf_skb_htod_tag_get_field(p_tail, FREERUN);
            /* be sure to handle wrap of 256 */
            if (((fr_new > fr_compare) && ((fr_new - fr_compare) < 128)) ||
                    ((fr_new < fr_compare) && ((fr_compare - fr_new) > 128))) {
                break;
            }
            p_tail = brcmf_netbuf_list_prev(queue, p_tail);
        }
        /* Position found. Determine what to do */
        if (p_tail == NULL) {
            /* empty list */
            brcmf_netbuf_add_tail_locked(queue, p);
        } else {
            fr_compare = brcmf_skb_htod_tag_get_field(p_tail, FREERUN);
            if (((fr_new > fr_compare) && ((fr_new - fr_compare) < 128)) ||
                    ((fr_new < fr_compare) && ((fr_compare - fr_new) > 128))) {
                /* After tail */
                brcmf_netbuf_add_after_locked(queue, p_tail, p);
            } else {
                /* Before tail */
                brcmf_netbuf_add_after_locked(queue, p_tail->prev, p);
            }
        }

        /* Complete the counters and statistics */
        pq->len++;
        if (pq->hi_prec < prec) {
            pq->hi_prec = (uint8_t)prec;
        }
    } else if (brcmu_pktq_penq(&entry->psq, prec, p) == NULL) {
        *qfull_stat += 1;
        return ZX_ERR_NO_RESOURCES;
    }

    /* increment total enqueued packet count */
    fws->fifo_delay_map |= 1 << fifo;
    fws->fifo_enqpkt[fifo]++;

    /* update the brcmf_netbuf state */
    brcmf_workspace(p)->state = state;

    /*
     * A packet has been pushed so update traffic
     * availability bitmap, if applicable
     */
    brcmf_fws_tim_update(fws, entry, fifo, true);
    brcmf_fws_flow_control_check(fws, &entry->psq, brcmf_skb_if_flags_get_field(p, INDEX));
    return ZX_OK;
}

static struct brcmf_netbuf* brcmf_fws_deq(struct brcmf_fws_info* fws, int fifo) {
    struct brcmf_fws_mac_descriptor* table;
    struct brcmf_fws_mac_descriptor* entry;
    struct brcmf_netbuf* p;
    int num_nodes;
    int node_pos;
    int prec_out;
    int pmsk;
    int i;

    table = (struct brcmf_fws_mac_descriptor*)&fws->desc;
    num_nodes = sizeof(fws->desc) / sizeof(struct brcmf_fws_mac_descriptor);
    node_pos = fws->deq_node_pos[fifo];

    for (i = 0; i < num_nodes; i++) {
        entry = &table[(node_pos + i) % num_nodes];
        if (!entry->occupied || brcmf_fws_macdesc_closed(fws, entry, fifo)) {
            continue;
        }

        if (entry->suppressed) {
            pmsk = 2;
        } else {
            pmsk = 3;
        }
        p = brcmu_pktq_mdeq(&entry->psq, pmsk << (fifo * 2), &prec_out);
        if (p == NULL) {
            if (entry->suppressed) {
                if (entry->suppr_transit_count) {
                    continue;
                }
                entry->suppressed = false;
                p = brcmu_pktq_mdeq(&entry->psq, 1 << (fifo * 2), &prec_out);
            }
        }
        if (p == NULL) {
            continue;
        }

        brcmf_fws_macdesc_use_req_credit(entry, p);

        /* move dequeue position to ensure fair round-robin */
        fws->deq_node_pos[fifo] = (node_pos + i + 1) % num_nodes;
        brcmf_fws_flow_control_check(fws, &entry->psq, brcmf_skb_if_flags_get_field(p, INDEX));
        /*
         * A packet has been picked up, update traffic
         * availability bitmap, if applicable
         */
        brcmf_fws_tim_update(fws, entry, fifo, false);

        /*
         * decrement total enqueued fifo packets and
         * clear delay bitmap if done.
         */
        fws->fifo_enqpkt[fifo]--;
        if (fws->fifo_enqpkt[fifo] == 0) {
            fws->fifo_delay_map &= ~(1 << fifo);
        }
        goto done;
    }
    p = NULL;
done:
    brcmf_dbg(DATA, "exit: fifo %d netbuf %p\n", fifo, p);
    return p;
}

static zx_status_t brcmf_fws_txstatus_suppressed(struct brcmf_fws_info* fws, int fifo,
                                                 struct brcmf_netbuf* netbuf, uint32_t genbit,
                                                 uint16_t seq) {
    struct brcmf_fws_mac_descriptor* entry = brcmf_workspace(netbuf)->mac;
    uint32_t hslot;
    zx_status_t ret;

    hslot = brcmf_skb_htod_tag_get_field(netbuf, HSLOT);

    /* this packet was suppressed */
    if (!entry->suppressed) {
        entry->suppressed = true;
        entry->suppr_transit_count = entry->transit_count;
        brcmf_dbg(DATA, "suppress %s: transit %d\n", entry->name, entry->transit_count);
    }

    entry->generation = genbit;

    brcmf_skb_htod_tag_set_field(netbuf, GENERATION, genbit);
    brcmf_workspace(netbuf)->htod_seq = seq;
    if (brcmf_skb_htod_seq_get_field(netbuf, FROMFW)) {
        brcmf_skb_htod_seq_set_field(netbuf, FROMDRV, 1);
        brcmf_skb_htod_seq_set_field(netbuf, FROMFW, 0);
    } else {
        brcmf_skb_htod_seq_set_field(netbuf, FROMDRV, 0);
    }
    ret = brcmf_fws_enq(fws, BRCMF_FWS_SKBSTATE_SUPPRESSED, fifo, netbuf);

    if (ret != ZX_OK) {
        /* suppress q is full drop this packet */
        brcmf_fws_hanger_poppkt(&fws->hanger, hslot, &netbuf, true);
    } else {
        /* Mark suppressed to avoid a double free during wlfc cleanup */
        brcmf_fws_hanger_mark_suppressed(&fws->hanger, hslot);
    }

    return ret;
}

static zx_status_t brcmf_fws_txs_process(struct brcmf_fws_info* fws, uint8_t flags, uint32_t hslot,
                                         uint32_t genbit, uint16_t seq) {
    uint32_t fifo;
    zx_status_t ret;
    bool remove_from_hanger = true;
    struct brcmf_netbuf* netbuf;
    struct brcmf_netbuf_workspace* workspace;
    struct brcmf_fws_mac_descriptor* entry = NULL;
    struct brcmf_if* ifp;

    brcmf_dbg(DATA, "flags %d\n", flags);

    if (flags == BRCMF_FWS_TXSTATUS_DISCARD) {
        fws->stats.txs_discard++;
    } else if (flags == BRCMF_FWS_TXSTATUS_CORE_SUPPRESS) {
        fws->stats.txs_supp_core++;
        remove_from_hanger = false;
    } else if (flags == BRCMF_FWS_TXSTATUS_FW_PS_SUPPRESS) {
        fws->stats.txs_supp_ps++;
        remove_from_hanger = false;
    } else if (flags == BRCMF_FWS_TXSTATUS_FW_TOSSED) {
        fws->stats.txs_tossed++;
    } else if (flags == BRCMF_FWS_TXSTATUS_HOST_TOSSED) {
        fws->stats.txs_host_tossed++;
    } else {
        brcmf_err("unexpected txstatus\n");
    }

    ret = brcmf_fws_hanger_poppkt(&fws->hanger, hslot, &netbuf, remove_from_hanger);
    if (ret != ZX_OK) {
        brcmf_err("no packet in hanger slot: hslot=%d\n", hslot);
        return ret;
    }

    workspace = brcmf_workspace(netbuf);
    // TODO(cphoenix): Used to be WARN_ON(!entry) which wouldn't have picked up entry==error
    // anyway. Revisit logic here.
    if (workspace->mac_status == ZX_OK) {
        entry = workspace->mac;
    } else {
        WARN_ON(true/*bad entry*/);
        brcmu_pkt_buf_free_skb(netbuf);
        return ZX_ERR_INTERNAL;
    }
    entry->transit_count--;
    if (entry->suppressed && entry->suppr_transit_count) {
        entry->suppr_transit_count--;
    }

    brcmf_dbg(DATA, "%s flags %d htod %X seq %X\n", entry->name, flags, workspace->htod, seq);

    /* pick up the implicit credit from this packet */
    fifo = brcmf_skb_htod_tag_get_field(netbuf, FIFO);
    if ((fws->fcmode == BRCMF_FWS_FCMODE_IMPLIED_CREDIT) ||
            (brcmf_skb_if_flags_get_field(netbuf, REQ_CREDIT)) ||
            (flags == BRCMF_FWS_TXSTATUS_HOST_TOSSED)) {
        brcmf_fws_return_credits(fws, fifo, 1);
        brcmf_fws_schedule_deq(fws);
    }
    brcmf_fws_macdesc_return_req_credit(netbuf);

    ret = brcmf_proto_hdrpull(fws->drvr, false, netbuf, &ifp);
    if (ret != ZX_OK) {
        brcmu_pkt_buf_free_skb(netbuf);
        return ZX_ERR_INTERNAL;
    }
    if (!remove_from_hanger) {
        ret = brcmf_fws_txstatus_suppressed(fws, fifo, netbuf, genbit, seq);
    }
    if (remove_from_hanger || ret != ZX_OK) {
        brcmf_txfinalize(ifp, netbuf, true);
    }

    return ZX_OK;
}

static enum brcmf_fws_should_schedule brcmf_fws_fifocreditback_indicate(struct brcmf_fws_info* fws,
                                                                        uint8_t* data) {
    int i;

    if (fws->fcmode != BRCMF_FWS_FCMODE_EXPLICIT_CREDIT) {
        brcmf_dbg(INFO, "ignored\n");
        return BRCMF_FWS_NOSCHEDULE;
    }

    brcmf_dbg(DATA, "enter: data %pM\n", data);
    brcmf_fws_lock(fws);
    for (i = 0; i < BRCMF_FWS_FIFO_COUNT; i++) {
        brcmf_fws_return_credits(fws, i, data[i]);
    }

    brcmf_dbg(DATA, "map: credit %x delay %x\n", fws->fifo_credit_map, fws->fifo_delay_map);
    brcmf_fws_unlock(fws);
    return BRCMF_FWS_SCHEDULE;
}

static enum brcmf_fws_should_schedule brcmf_fws_txstatus_indicate(struct brcmf_fws_info* fws,
                                                                  uint8_t* data) {
    uint32_t status_le;
    uint16_t seq_le;
    uint32_t status;
    uint32_t hslot;
    uint32_t genbit;
    uint8_t flags;
    uint16_t seq;

    fws->stats.txs_indicate++;
    memcpy(&status_le, data, sizeof(status_le));
    status = status_le;
    flags = brcmf_txstatus_get_field(status, FLAGS);
    hslot = brcmf_txstatus_get_field(status, HSLOT);
    genbit = brcmf_txstatus_get_field(status, GENERATION);
    if (BRCMF_FWS_MODE_GET_REUSESEQ(fws->mode)) {
        memcpy(&seq_le, &data[BRCMF_FWS_TYPE_PKTTAG_LEN], sizeof(seq_le));
        seq = seq_le;
    } else {
        seq = 0;
    }

    brcmf_fws_lock(fws);
    brcmf_fws_txs_process(fws, flags, hslot, genbit, seq);
    brcmf_fws_unlock(fws);
    return BRCMF_FWS_NOSCHEDULE;
}

static zx_status_t brcmf_fws_dbg_seqnum_check(struct brcmf_fws_info* fws, uint8_t* data) {
    uint32_t timestamp;

    memcpy(&timestamp, &data[2], sizeof(timestamp));
    brcmf_dbg(CTL, "received: seq %d, timestamp %d\n", data[1], timestamp);
    return ZX_OK;
}

static zx_status_t brcmf_fws_notify_credit_map(struct brcmf_if* ifp,
                                               const struct brcmf_event_msg* e, void* data) {
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);
    int i;
    uint8_t* credits = data;

    if (e->datalen < BRCMF_FWS_FIFO_COUNT) {
        brcmf_err("event payload too small (%d)\n", e->datalen);
        return ZX_ERR_INVALID_ARGS;
    }
    if (fws->creditmap_received) {
        return ZX_OK;
    }

    fws->creditmap_received = true;

    brcmf_dbg(TRACE, "enter: credits %pM\n", credits);
    brcmf_fws_lock(fws);
    for (i = 0; i < (int)ARRAY_SIZE(fws->fifo_credit); i++) {
        if (*credits) {
            fws->fifo_credit_map |= 1 << i;
        } else {
            fws->fifo_credit_map &= ~(1 << i);
        }
        fws->fifo_credit[i] = *credits++;
    }
    brcmf_fws_schedule_deq(fws);
    brcmf_fws_unlock(fws);
    return ZX_OK;
}

static zx_status_t brcmf_fws_notify_bcmc_credit_support(struct brcmf_if* ifp,
                                                        const struct brcmf_event_msg* e,
                                                        void* data) {
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);

    if (fws) {
        brcmf_fws_lock(fws);
        fws->bcmc_credit_check = true;
        brcmf_fws_unlock(fws);
    }
    return ZX_OK;
}

static void brcmf_rxreorder_get_skb_list(struct brcmf_ampdu_rx_reorder* rfi, uint8_t start,
                                         uint8_t end, struct brcmf_netbuf_list* skb_list) {
    /* initialize return list */
    brcmf_netbuf_list_init_nonlocked(skb_list);

    if (rfi->pend_pkts == 0) {
        brcmf_dbg(INFO, "no packets in reorder queue\n");
        return;
    }

    do {
        if (rfi->pktslots[start]) {
            brcmf_netbuf_add_tail_locked(skb_list, rfi->pktslots[start]);
            rfi->pktslots[start] = NULL;
        }
        start++;
        if (start > rfi->max_idx) {
            start = 0;
        }
    } while (start != end);
    rfi->pend_pkts -= brcmf_netbuf_list_length(skb_list);
}

void brcmf_fws_rxreorder(struct brcmf_if* ifp, struct brcmf_netbuf* pkt) {
    uint8_t* reorder_data;
    uint8_t flow_id, max_idx, cur_idx, exp_idx, end_idx;
    struct brcmf_ampdu_rx_reorder* rfi;
    struct brcmf_netbuf_list reorder_list;
    struct brcmf_netbuf* pnext;
    uint8_t flags;
    uint32_t buf_size;

    reorder_data = ((struct brcmf_skb_reorder_data*)pkt->workspace)->reorder;
    flow_id = reorder_data[BRCMF_RXREORDER_FLOWID_OFFSET];
    flags = reorder_data[BRCMF_RXREORDER_FLAGS_OFFSET];

    /* validate flags and flow id */
    if (flags == 0xFF) {
        brcmf_err("invalid flags...so ignore this packet\n");
        brcmf_netif_rx(ifp, pkt);
        return;
    }

    rfi = ifp->drvr->reorder_flows[flow_id];
    if (flags & BRCMF_RXREORDER_DEL_FLOW) {
        brcmf_dbg(INFO, "flow-%d: delete\n", flow_id);

        if (rfi == NULL) {
            brcmf_dbg(INFO, "received flags to cleanup, but no flow (%d) yet\n", flow_id);
            brcmf_netif_rx(ifp, pkt);
            return;
        }

        brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, rfi->exp_idx, &reorder_list);
        /* add the last packet */
        brcmf_netbuf_add_tail_locked(&reorder_list, pkt);
        free(rfi);
        ifp->drvr->reorder_flows[flow_id] = NULL;
        goto netif_rx;
    }
    /* from here on we need a flow reorder instance */
    if (rfi == NULL) {
        buf_size = sizeof(*rfi);
        max_idx = reorder_data[BRCMF_RXREORDER_MAXIDX_OFFSET];

        buf_size += (max_idx + 1) * sizeof(pkt);

        /* allocate space for flow reorder info */
        brcmf_dbg(INFO, "flow-%d: start, maxidx %d\n", flow_id, max_idx);
        rfi = calloc(1, buf_size);
        if (rfi == NULL) {
            brcmf_err("failed to alloc buffer\n");
            brcmf_netif_rx(ifp, pkt);
            return;
        }

        ifp->drvr->reorder_flows[flow_id] = rfi;
        rfi->pktslots = (struct brcmf_netbuf**)(rfi + 1);
        rfi->max_idx = max_idx;
    }
    if (flags & BRCMF_RXREORDER_NEW_HOLE) {
        if (rfi->pend_pkts) {
            brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, rfi->exp_idx, &reorder_list);
            WARN_ON(rfi->pend_pkts);
        } else {
            brcmf_netbuf_list_init_nonlocked(&reorder_list);
        }
        rfi->cur_idx = reorder_data[BRCMF_RXREORDER_CURIDX_OFFSET];
        rfi->exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];
        rfi->max_idx = reorder_data[BRCMF_RXREORDER_MAXIDX_OFFSET];
        rfi->pktslots[rfi->cur_idx] = pkt;
        rfi->pend_pkts++;
        brcmf_dbg(DATA, "flow-%d: new hole %d (%d), pending %d\n", flow_id, rfi->cur_idx,
                  rfi->exp_idx, rfi->pend_pkts);
    } else if (flags & BRCMF_RXREORDER_CURIDX_VALID) {
        cur_idx = reorder_data[BRCMF_RXREORDER_CURIDX_OFFSET];
        exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];

        if ((exp_idx == rfi->exp_idx) && (cur_idx != rfi->exp_idx)) {
            /* still in the current hole */
            /* enqueue the current on the buffer chain */
            if (rfi->pktslots[cur_idx] != NULL) {
                brcmf_dbg(INFO, "HOLE: ERROR buffer pending..free it\n");
                brcmu_pkt_buf_free_skb(rfi->pktslots[cur_idx]);
                rfi->pktslots[cur_idx] = NULL;
            }
            rfi->pktslots[cur_idx] = pkt;
            rfi->pend_pkts++;
            rfi->cur_idx = cur_idx;
            brcmf_dbg(DATA, "flow-%d: store pkt %d (%d), pending %d\n", flow_id, cur_idx, exp_idx,
                      rfi->pend_pkts);

            /* can return now as there is no reorder
             * list to process.
             */
            return;
        }
        if (rfi->exp_idx == cur_idx) {
            if (rfi->pktslots[cur_idx] != NULL) {
                brcmf_dbg(INFO, "error buffer pending..free it\n");
                brcmu_pkt_buf_free_skb(rfi->pktslots[cur_idx]);
                rfi->pktslots[cur_idx] = NULL;
            }
            rfi->pktslots[cur_idx] = pkt;
            rfi->pend_pkts++;

            /* got the expected one. flush from current to expected
             * and update expected
             */
            brcmf_dbg(DATA, "flow-%d: expected %d (%d), pending %d\n", flow_id, cur_idx, exp_idx,
                      rfi->pend_pkts);

            rfi->cur_idx = cur_idx;
            rfi->exp_idx = exp_idx;

            brcmf_rxreorder_get_skb_list(rfi, cur_idx, exp_idx, &reorder_list);
            brcmf_dbg(DATA, "flow-%d: freeing buffers %d, pending %d\n", flow_id,
                      brcmf_netbuf_list_length(&reorder_list), rfi->pend_pkts);
        } else {
            uint8_t end_idx;

            brcmf_dbg(DATA, "flow-%d (0x%x): both moved, old %d/%d, new %d/%d\n", flow_id, flags,
                      rfi->cur_idx, rfi->exp_idx, cur_idx, exp_idx);
            if (flags & BRCMF_RXREORDER_FLUSH_ALL) {
                end_idx = rfi->exp_idx;
            } else {
                end_idx = exp_idx;
            }

            /* flush pkts first */
            brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, end_idx, &reorder_list);

            if (exp_idx == ((cur_idx + 1) % (rfi->max_idx + 1))) {
                brcmf_netbuf_add_tail_locked(&reorder_list, pkt);
            } else {
                rfi->pktslots[cur_idx] = pkt;
                rfi->pend_pkts++;
            }
            rfi->exp_idx = exp_idx;
            rfi->cur_idx = cur_idx;
        }
    } else {
        /* explicity window move updating the expected index */
        exp_idx = reorder_data[BRCMF_RXREORDER_EXPIDX_OFFSET];

        brcmf_dbg(DATA, "flow-%d (0x%x): change expected: %d -> %d\n", flow_id, flags, rfi->exp_idx,
                  exp_idx);
        if (flags & BRCMF_RXREORDER_FLUSH_ALL) {
            end_idx = rfi->exp_idx;
        } else {
            end_idx = exp_idx;
        }

        brcmf_rxreorder_get_skb_list(rfi, rfi->exp_idx, end_idx, &reorder_list);
        brcmf_netbuf_add_tail_locked(&reorder_list, pkt);
        /* set the new expected idx */
        rfi->exp_idx = exp_idx;
    }
netif_rx:
    brcmf_netbuf_list_for_every_safe(&reorder_list, pkt, pnext) {
        brcmf_netbuf_list_remove_locked(pkt, &reorder_list);
        brcmf_netif_rx(ifp, pkt);
    }
}

void brcmf_fws_hdrpull(struct brcmf_if* ifp, int16_t siglen, struct brcmf_netbuf* netbuf) {
    struct brcmf_skb_reorder_data* rd;
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);
    uint8_t* signal_data;
    int16_t data_len;
    uint8_t type;
    uint8_t len;
    uint8_t* data;
    int tlv_len;
    zx_status_t err;
    enum brcmf_fws_should_schedule schedule_status;

    brcmf_dbg(HDRS, "enter: ifidx %d, skblen %u, siglen %d\n", ifp->ifidx, netbuf->len, siglen);

    WARN_ON(siglen > netbuf->len);

    if (!siglen) {
        return;
    }
    /* if flow control disabled, skip to packet data and leave */
    if ((!fws) || (!fws->fw_signals)) {
        brcmf_netbuf_shrink_head(netbuf, siglen);
        return;
    }

    fws->stats.header_pulls++;
    data_len = siglen;
    signal_data = netbuf->data;

    schedule_status = BRCMF_FWS_NOSCHEDULE;
    while (data_len > 0) {
        /* extract tlv info */
        type = signal_data[0];

        /* FILLER type is actually not a TLV, but
         * a single byte that can be skipped.
         */
        if (type == BRCMF_FWS_TYPE_FILLER) {
            signal_data += 1;
            data_len -= 1;
            continue;
        }
        len = signal_data[1];
        data = signal_data + 2;

        err = brcmf_fws_get_tlv_len(fws, type, &tlv_len);
        brcmf_dbg(HDRS, "tlv type=%s (%d), len=%d (%d:%d)\n", brcmf_fws_get_tlv_name(type), type,
                  len, err, tlv_len);

        /* abort parsing when length invalid */
        if (data_len < len + 2) {
            break;
        }

        err = brcmf_fws_get_tlv_len(fws, type, &tlv_len);
        if (err != ZX_OK || len < tlv_len) {
            break;
        }

        switch (type) {
        case BRCMF_FWS_TYPE_COMP_TXSTATUS:
            break;
        case BRCMF_FWS_TYPE_HOST_REORDER_RXPKTS:
            rd = (struct brcmf_skb_reorder_data*)netbuf->workspace;
            rd->reorder = data;
            break;
        case BRCMF_FWS_TYPE_MACDESC_ADD:
        case BRCMF_FWS_TYPE_MACDESC_DEL:
            brcmf_fws_macdesc_indicate(fws, type, data);
            break;
        case BRCMF_FWS_TYPE_MAC_OPEN:
        case BRCMF_FWS_TYPE_MAC_CLOSE:
            schedule_status = brcmf_fws_macdesc_state_indicate(fws, type, data);
            break;
        case BRCMF_FWS_TYPE_INTERFACE_OPEN:
        case BRCMF_FWS_TYPE_INTERFACE_CLOSE:
            schedule_status = brcmf_fws_interface_state_indicate(fws, type, data);
            break;
        case BRCMF_FWS_TYPE_MAC_REQUEST_CREDIT:
        case BRCMF_FWS_TYPE_MAC_REQUEST_PACKET:
            schedule_status = brcmf_fws_request_indicate(fws, type, data);
            break;
        case BRCMF_FWS_TYPE_TXSTATUS:
            // TODO(cphoenix): Should this set schedule_status?
            brcmf_fws_txstatus_indicate(fws, data);
            break;
        case BRCMF_FWS_TYPE_FIFO_CREDITBACK:
            schedule_status = brcmf_fws_fifocreditback_indicate(fws, data);
            break;
        case BRCMF_FWS_TYPE_RSSI:
            brcmf_fws_rssi_indicate(fws, *data);
            break;
        case BRCMF_FWS_TYPE_TRANS_ID:
            brcmf_fws_dbg_seqnum_check(fws, data);
            break;
        case BRCMF_FWS_TYPE_PKTTAG:
        case BRCMF_FWS_TYPE_PENDING_TRAFFIC_BMP:
        default:
            fws->stats.tlv_invalid_type++;
            break;
        }
        signal_data += len + 2;
        data_len -= len + 2;
    }

    if (data_len != 0) {
        fws->stats.tlv_parse_failed++;
    }

    if (schedule_status == BRCMF_FWS_SCHEDULE) {
        brcmf_fws_schedule_deq(fws);
    }

    /* signalling processing result does
     * not affect the actual ethernet packet.
     */
    brcmf_netbuf_shrink_head(netbuf, siglen);

    /* this may be a signal-only packet
     */
    if (netbuf->len == 0) {
        fws->stats.header_only_pkt++;
    }
}

static uint8_t brcmf_fws_precommit_skb(struct brcmf_fws_info* fws, int fifo,
                                       struct brcmf_netbuf* p) {
    struct brcmf_netbuf_workspace* workspace = brcmf_workspace(p);
    // TODO(cphoenix): workspace->mac and mac_status are often unchecked in this file.
    // Be more paranoid?
    struct brcmf_fws_mac_descriptor* entry = workspace->mac;
    uint8_t flags;

    if (workspace->state != BRCMF_FWS_SKBSTATE_SUPPRESSED) {
        brcmf_skb_htod_tag_set_field(p, GENERATION, entry->generation);
    }
    flags = BRCMF_FWS_HTOD_FLAG_PKTFROMHOST;
    if (brcmf_skb_if_flags_get_field(p, REQUESTED)) {
        /*
         * Indicate that this packet is being sent in response to an
         * explicit request from the firmware side.
         */
        flags |= BRCMF_FWS_HTOD_FLAG_PKT_REQUESTED;
    }
    brcmf_skb_htod_tag_set_field(p, FLAGS, flags);
    return brcmf_fws_hdrpush(fws, p);
}

static void brcmf_fws_rollback_toq(struct brcmf_fws_info* fws, struct brcmf_netbuf* netbuf, int fifo) {
    struct brcmf_fws_mac_descriptor* entry;
    struct brcmf_netbuf* pktout;
    int qidx, hslot;
    zx_status_t rc = ZX_OK;

    entry = brcmf_workspace(netbuf)->mac;
    if (entry->occupied) {
        qidx = 2 * fifo;
        if (brcmf_workspace(netbuf)->state == BRCMF_FWS_SKBSTATE_SUPPRESSED) {
            qidx++;
        }

        pktout = brcmu_pktq_penq_head(&entry->psq, qidx, netbuf);
        if (pktout == NULL) {
            brcmf_err("%s queue %d full\n", entry->name, qidx);
            rc = ZX_ERR_NO_RESOURCES;
        }
    } else {
        brcmf_err("%s entry removed\n", entry->name);
        rc = ZX_ERR_NOT_FOUND;
    }

    if (rc != ZX_OK) {
        fws->stats.rollback_failed++;
        hslot = brcmf_skb_htod_tag_get_field(netbuf, HSLOT);
        brcmf_fws_txs_process(fws, BRCMF_FWS_TXSTATUS_HOST_TOSSED, hslot, 0, 0);
    } else {
        fws->stats.rollback_success++;
        brcmf_fws_return_credits(fws, fifo, 1);
        brcmf_fws_macdesc_return_req_credit(netbuf);
    }
}

static zx_status_t brcmf_fws_borrow_credit(struct brcmf_fws_info* fws) {
    int lender_ac;

    if (fws->borrow_defer_timestamp > zx_clock_get(ZX_CLOCK_MONOTONIC)) {
        fws->fifo_credit_map &= ~(1 << BRCMF_FWS_FIFO_AC_BE);
        return ZX_ERR_UNAVAILABLE;
    }

    for (lender_ac = 0; lender_ac <= BRCMF_FWS_FIFO_AC_VO; lender_ac++) {
        if (fws->fifo_credit[lender_ac]) {
            fws->credits_borrowed[lender_ac]++;
            fws->fifo_credit[lender_ac]--;
            if (fws->fifo_credit[lender_ac] == 0) {
                fws->fifo_credit_map &= ~(1 << lender_ac);
            }
            fws->fifo_credit_map |= (1 << BRCMF_FWS_FIFO_AC_BE);
            brcmf_dbg(DATA, "borrow credit from: %d\n", lender_ac);
            return ZX_OK;
        }
    }
    fws->fifo_credit_map &= ~(1 << BRCMF_FWS_FIFO_AC_BE);
    return ZX_ERR_UNAVAILABLE;
}

static zx_status_t brcmf_fws_commit_skb(struct brcmf_fws_info* fws, int fifo,
                                        struct brcmf_netbuf* netbuf) {
    struct brcmf_netbuf_workspace* workspace = brcmf_workspace(netbuf);
    struct brcmf_fws_mac_descriptor* entry;
    zx_status_t rc;
    uint8_t ifidx;
    uint8_t data_offset;

    if (workspace->mac_status != ZX_OK) {
        return workspace->mac_status;
    }
    entry = workspace->mac;

    data_offset = brcmf_fws_precommit_skb(fws, fifo, netbuf);
    entry->transit_count++;
    if (entry->suppressed) {
        entry->suppr_transit_count++;
    }
    ifidx = brcmf_skb_if_flags_get_field(netbuf, INDEX);
    brcmf_fws_unlock(fws);
    rc = brcmf_proto_txdata(fws->drvr, ifidx, data_offset, netbuf);
    brcmf_fws_lock(fws);
    brcmf_dbg(DATA, "%s flags %X htod %X bus_tx %d\n", entry->name, workspace->if_flags,
              workspace->htod, rc);
    if (rc != ZX_OK) {
        entry->transit_count--;
        if (entry->suppressed) {
            entry->suppr_transit_count--;
        }
        (void)brcmf_proto_hdrpull(fws->drvr, false, netbuf, NULL);
        goto rollback;
    }

    fws->stats.pkt2bus++;
    fws->stats.send_pkts[fifo]++;
    if (brcmf_skb_if_flags_get_field(netbuf, REQUESTED)) {
        fws->stats.requested_sent[fifo]++;
    }

    return rc;

rollback:
    brcmf_fws_rollback_toq(fws, netbuf, fifo);
    return rc;
}

static zx_status_t brcmf_fws_assign_htod(struct brcmf_fws_info* fws, struct brcmf_netbuf* p,
                                         int fifo) {
    struct brcmf_netbuf_workspace* workspace = brcmf_workspace(p);
    zx_status_t rc;
    int hslot;

    workspace->htod = 0;
    workspace->htod_seq = 0;
    hslot = brcmf_fws_hanger_get_free_slot(&fws->hanger);
    brcmf_skb_htod_tag_set_field(p, HSLOT, hslot);
    brcmf_skb_htod_tag_set_field(p, FREERUN, workspace->mac->seq[fifo]);
    brcmf_skb_htod_tag_set_field(p, FIFO, fifo);
    rc = brcmf_fws_hanger_pushpkt(&fws->hanger, p, hslot);
    if (rc == ZX_OK) {
        workspace->mac->seq[fifo]++;
    } else {
        fws->stats.generic_error++;
    }
    return rc;
}

zx_status_t brcmf_fws_process_skb(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);
    struct brcmf_netbuf_workspace* workspace = brcmf_workspace(netbuf);
    struct ethhdr* eh = (struct ethhdr*)(netbuf->data);
    int fifo = BRCMF_FWS_FIFO_BCMC;
    bool multicast = address_is_multicast(eh->h_dest);
    zx_status_t rc = ZX_OK;

    brcmf_dbg(DATA, "tx proto=0x%X\n", be16toh(eh->h_proto));

    /* set control buffer information */
    workspace->if_flags = 0;
    workspace->state = BRCMF_FWS_SKBSTATE_NEW;
    brcmf_skb_if_flags_set_field(netbuf, INDEX, ifp->ifidx);
    if (!multicast) {
        fifo = brcmf_fws_prio2fifo[netbuf->priority];
    }

    brcmf_fws_lock(fws);
    if (fifo != BRCMF_FWS_FIFO_AC_BE && fifo < BRCMF_FWS_FIFO_BCMC) {
        fws->borrow_defer_timestamp = zx_clock_get(ZX_CLOCK_MONOTONIC) +
                                      ZX_MSEC(BRCMF_FWS_BORROW_DEFER_PERIOD_MSEC);
    }

    workspace->mac_status = brcmf_fws_macdesc_find(fws, ifp, eh->h_dest, &workspace->mac);
    brcmf_dbg(DATA, "%s mac %pM multi %d fifo %d\n", workspace->mac->name, eh->h_dest, multicast,
              fifo);
    if (brcmf_fws_assign_htod(fws, netbuf, fifo) == ZX_OK) {
        brcmf_fws_enq(fws, BRCMF_FWS_SKBSTATE_DELAYED, fifo, netbuf);
        brcmf_fws_schedule_deq(fws);
    } else {
        brcmf_err("drop netbuf: no hanger slot\n");
        brcmf_txfinalize(ifp, netbuf, false);
        rc = ZX_ERR_NO_MEMORY;
    }
    brcmf_fws_unlock(fws);

    return rc;
}

void brcmf_fws_reset_interface(struct brcmf_if* ifp) {
    struct brcmf_fws_mac_descriptor* entry = ifp->fws_desc;

    brcmf_dbg(TRACE, "enter: bsscfgidx=%d\n", ifp->bsscfgidx);
    if (!entry) {
        return;
    }

    brcmf_fws_macdesc_init(entry, ifp->mac_addr, ifp->ifidx);
}

void brcmf_fws_add_interface(struct brcmf_if* ifp) {
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);
    struct brcmf_fws_mac_descriptor* entry;

    if (!ifp->ndev || !brcmf_fws_queue_skbs(fws)) {
        return;
    }

    entry = &fws->desc.iface[ifp->ifidx];
    ifp->fws_desc = entry;
    brcmf_fws_macdesc_init(entry, ifp->mac_addr, ifp->ifidx);
    brcmf_fws_macdesc_set_name(fws, entry);
    brcmu_pktq_init(&entry->psq, BRCMF_FWS_PSQ_PREC_COUNT, BRCMF_FWS_PSQ_LEN);
    brcmf_dbg(TRACE, "added %s\n", entry->name);
}

void brcmf_fws_del_interface(struct brcmf_if* ifp) {
    struct brcmf_fws_mac_descriptor* entry = ifp->fws_desc;
    struct brcmf_fws_info* fws = drvr_to_fws(ifp->drvr);

    if (!entry) {
        return;
    }

    brcmf_fws_lock(fws);
    ifp->fws_desc = NULL;
    brcmf_dbg(TRACE, "deleting %s\n", entry->name);
    brcmf_fws_macdesc_deinit(entry);
    brcmf_fws_cleanup(fws, ifp->ifidx);
    brcmf_fws_unlock(fws);
}

static void brcmf_fws_dequeue_worker(struct work_struct* worker) {
    struct brcmf_fws_info* fws;
    struct brcmf_pub* drvr;
    struct brcmf_netbuf* netbuf;
    int fifo;
    uint32_t hslot;
    uint32_t ifidx;
    zx_status_t ret;

    fws = container_of(worker, struct brcmf_fws_info, fws_dequeue_work);
    drvr = fws->drvr;

    brcmf_fws_lock(fws);
    for (fifo = BRCMF_FWS_FIFO_BCMC; fifo >= 0 && !fws->bus_flow_blocked; fifo--) {
        if (!brcmf_fws_fc_active(fws)) {
            while ((netbuf = brcmf_fws_deq(fws, fifo)) != NULL) {
                hslot = brcmf_skb_htod_tag_get_field(netbuf, HSLOT);
                brcmf_fws_hanger_poppkt(&fws->hanger, hslot, &netbuf, true);
                ifidx = brcmf_skb_if_flags_get_field(netbuf, INDEX);
                /* Use proto layer to send data frame */
                brcmf_fws_unlock(fws);
                ret = brcmf_proto_txdata(drvr, ifidx, 0, netbuf);
                brcmf_fws_lock(fws);
                if (ret != ZX_OK) {
                    brcmf_txfinalize(brcmf_get_ifp(drvr, ifidx), netbuf, false);
                }
                if (fws->bus_flow_blocked) {
                    break;
                }
            }
            continue;
        }
        while ((fws->fifo_credit[fifo]) ||
                ((!fws->bcmc_credit_check) && (fifo == BRCMF_FWS_FIFO_BCMC))) {
            netbuf = brcmf_fws_deq(fws, fifo);
            if (!netbuf) {
                break;
            }
            fws->fifo_credit[fifo]--;
            if (brcmf_fws_commit_skb(fws, fifo, netbuf) != ZX_OK) {
                break;
            }
            if (fws->bus_flow_blocked) {
                break;
            }
        }
        if ((fifo == BRCMF_FWS_FIFO_AC_BE) && (fws->fifo_credit[fifo] == 0) &&
                (!fws->bus_flow_blocked)) {
            while (brcmf_fws_borrow_credit(fws) == ZX_OK) {
                netbuf = brcmf_fws_deq(fws, fifo);
                if (!netbuf) {
                    brcmf_fws_return_credits(fws, fifo, 1);
                    break;
                }
                if (brcmf_fws_commit_skb(fws, fifo, netbuf) != ZX_OK) {
                    break;
                }
                if (fws->bus_flow_blocked) {
                    break;
                }
            }
        }
    }
    brcmf_fws_unlock(fws);
}

#ifdef DEBUG
static zx_status_t brcmf_debugfs_fws_stats_read(struct seq_file* seq, void* data) {
    struct brcmf_bus* bus_if = dev_get_drvdata(seq->private);
    struct brcmf_fws_stats* fwstats = &(drvr_to_fws(bus_if->drvr)->stats);

    seq_printf(seq,
               "header_pulls:      %u\n"
               "header_only_pkt:   %u\n"
               "tlv_parse_failed:  %u\n"
               "tlv_invalid_type:  %u\n"
               "mac_update_fails:  %u\n"
               "ps_update_fails:   %u\n"
               "if_update_fails:   %u\n"
               "pkt2bus:           %u\n"
               "generic_error:     %u\n"
               "rollback_success:  %u\n"
               "rollback_failed:   %u\n"
               "delayq_full:       %u\n"
               "supprq_full:       %u\n"
               "txs_indicate:      %u\n"
               "txs_discard:       %u\n"
               "txs_suppr_core:    %u\n"
               "txs_suppr_ps:      %u\n"
               "txs_tossed:        %u\n"
               "txs_host_tossed:   %u\n"
               "bus_flow_block:    %u\n"
               "fws_flow_block:    %u\n"
               "send_pkts:         BK:%u BE:%u VO:%u VI:%u BCMC:%u\n"
               "requested_sent:    BK:%u BE:%u VO:%u VI:%u BCMC:%u\n",
               fwstats->header_pulls, fwstats->header_only_pkt, fwstats->tlv_parse_failed,
               fwstats->tlv_invalid_type, fwstats->mac_update_failed, fwstats->mac_ps_update_failed,
               fwstats->if_update_failed, fwstats->pkt2bus, fwstats->generic_error,
               fwstats->rollback_success, fwstats->rollback_failed, fwstats->delayq_full_error,
               fwstats->supprq_full_error, fwstats->txs_indicate, fwstats->txs_discard,
               fwstats->txs_supp_core, fwstats->txs_supp_ps, fwstats->txs_tossed,
               fwstats->txs_host_tossed, fwstats->bus_flow_block, fwstats->fws_flow_block,
               fwstats->send_pkts[0], fwstats->send_pkts[1], fwstats->send_pkts[2],
               fwstats->send_pkts[3], fwstats->send_pkts[4], fwstats->requested_sent[0],
               fwstats->requested_sent[1], fwstats->requested_sent[2], fwstats->requested_sent[3],
               fwstats->requested_sent[4]);

    return ZX_OK;
}
#else
static zx_status_t brcmf_debugfs_fws_stats_read(struct seq_file* seq, void* data) {
    return ZX_OK;
}
#endif

zx_status_t brcmf_fws_attach(struct brcmf_pub* drvr, struct brcmf_fws_info** fws_out) {
    struct brcmf_fws_info* fws;
    struct brcmf_if* ifp;
    uint32_t tlv = BRCMF_FWS_FLAGS_RSSI_SIGNALS;
    zx_status_t rc;
    uint32_t mode;

    if (fws_out) {
        *fws_out = NULL;
    }

    fws = calloc(1, sizeof(*fws));
    if (!fws) {
        rc = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    //spin_lock_init(&fws->spinlock);

    /* store drvr reference */
    fws->drvr = drvr;
    fws->fcmode = drvr->settings->fcmode;

    if ((drvr->bus_if->always_use_fws_queue == false) && (fws->fcmode == BRCMF_FWS_FCMODE_NONE)) {
        fws->avoid_queueing = true;
        brcmf_dbg(INFO, "FWS queueing will be avoided\n");
        if (fws_out) {
            *fws_out = fws;
        }
        return ZX_OK;
    }

    fws->fws_wq = workqueue_create("brcmf_fws_wq");
    if (fws->fws_wq == NULL) {
        brcmf_err("workqueue creation failed\n");
        rc = ZX_ERR_NO_RESOURCES;
        goto fail;
    }
    workqueue_init_work(&fws->fws_dequeue_work, brcmf_fws_dequeue_worker);

    /* enable firmware signalling if fcmode active */
    if (fws->fcmode != BRCMF_FWS_FCMODE_NONE)
        tlv |= BRCMF_FWS_FLAGS_XONXOFF_SIGNALS | BRCMF_FWS_FLAGS_CREDIT_STATUS_SIGNALS |
               BRCMF_FWS_FLAGS_HOST_PROPTXSTATUS_ACTIVE | BRCMF_FWS_FLAGS_HOST_RXREORDER_ACTIVE;

    rc = brcmf_fweh_register(drvr, BRCMF_E_FIFO_CREDIT_MAP, brcmf_fws_notify_credit_map);
    if (rc != ZX_OK) {
        brcmf_err("register credit map handler failed\n");
        goto fail;
    }
    rc = brcmf_fweh_register(drvr, BRCMF_E_BCMC_CREDIT_SUPPORT,
                             brcmf_fws_notify_bcmc_credit_support);
    if (rc != ZX_OK) {
        brcmf_err("register bcmc credit handler failed\n");
        brcmf_fweh_unregister(drvr, BRCMF_E_FIFO_CREDIT_MAP);
        goto fail;
    }

    /* Setting the iovar may fail if feature is unsupported
     * so leave the rc as is so driver initialization can
     * continue. Set mode back to none indicating not enabled.
     */
    fws->fw_signals = true;
    ifp = brcmf_get_ifp(drvr, 0);
    if (brcmf_fil_iovar_int_set(ifp, "tlv", tlv) != ZX_OK) {
        brcmf_err("failed to set bdcv2 tlv signaling\n");
        fws->fcmode = BRCMF_FWS_FCMODE_NONE;
        fws->fw_signals = false;
    }

    if (brcmf_fil_iovar_int_set(ifp, "ampdu_hostreorder", 1) != ZX_OK) {
        brcmf_dbg(INFO, "enabling AMPDU host-reorder failed\n");
    }

    /* Enable seq number reuse, if supported */
    if (brcmf_fil_iovar_int_get(ifp, "wlfc_mode", &mode) == ZX_OK) {
        if (BRCMF_FWS_MODE_GET_REUSESEQ(mode)) {
            mode = 0;
            BRCMF_FWS_MODE_SET_REUSESEQ(mode, 1);
            if (brcmf_fil_iovar_int_set(ifp, "wlfc_mode", mode) == ZX_OK) {
                BRCMF_FWS_MODE_SET_REUSESEQ(fws->mode, 1);
            }
        }
    }

    brcmf_fws_hanger_init(&fws->hanger);
    brcmf_fws_macdesc_init(&fws->desc.other, NULL, 0);
    brcmf_fws_macdesc_set_name(fws, &fws->desc.other);
    brcmf_dbg(INFO, "added %s\n", fws->desc.other.name);
    brcmu_pktq_init(&fws->desc.other.psq, BRCMF_FWS_PSQ_PREC_COUNT, BRCMF_FWS_PSQ_LEN);

    /* create debugfs file for statistics */
    brcmf_debugfs_add_entry(drvr, "fws_stats", brcmf_debugfs_fws_stats_read);

    brcmf_dbg(INFO, "%s bdcv2 tlv signaling [%x]\n", fws->fw_signals ? "enabled" : "disabled", tlv);
    if (fws_out) {
        *fws_out = fws;
    }
    return ZX_OK;

fail:
    brcmf_fws_detach(fws);
    return rc;
}

void brcmf_fws_detach(struct brcmf_fws_info* fws) {
    if (!fws) {
        return;
    }

    if (fws->fws_wq) {
        workqueue_destroy(fws->fws_wq);
    }

    /* cleanup */
    brcmf_fws_lock(fws);
    brcmf_fws_cleanup(fws, -1);
    brcmf_fws_unlock(fws);

    /* free top structure */
    free(fws);
}

bool brcmf_fws_queue_skbs(struct brcmf_fws_info* fws) {
    return !fws->avoid_queueing;
}

bool brcmf_fws_fc_active(struct brcmf_fws_info* fws) {
    if (!fws->creditmap_received) {
        return false;
    }

    return fws->fcmode != BRCMF_FWS_FCMODE_NONE;
}

void brcmf_fws_bustxfail(struct brcmf_fws_info* fws, struct brcmf_netbuf* netbuf) {
    uint32_t hslot;

    if (brcmf_workspace(netbuf)->state == BRCMF_FWS_SKBSTATE_TIM) {
        brcmu_pkt_buf_free_skb(netbuf);
        return;
    }
    brcmf_fws_lock(fws);
    hslot = brcmf_skb_htod_tag_get_field(netbuf, HSLOT);
    brcmf_fws_txs_process(fws, BRCMF_FWS_TXSTATUS_HOST_TOSSED, hslot, 0, 0);
    brcmf_fws_unlock(fws);
}

void brcmf_fws_bus_blocked(struct brcmf_pub* drvr, bool flow_blocked) {
    struct brcmf_fws_info* fws = drvr_to_fws(drvr);
    struct brcmf_if* ifp;
    int i;

    if (fws->avoid_queueing) {
        for (i = 0; i < BRCMF_MAX_IFS; i++) {
            ifp = drvr->iflist[i];
            if (!ifp || !ifp->ndev) {
                continue;
            }
            brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_FLOW, flow_blocked);
        }
    } else {
        fws->bus_flow_blocked = flow_blocked;
        if (!flow_blocked) {
            brcmf_fws_schedule_deq(fws);
        } else {
            fws->stats.bus_flow_block++;
        }
    }
}
