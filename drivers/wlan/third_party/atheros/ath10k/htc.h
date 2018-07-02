/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HTC_H_
#define _HTC_H_

#include <ddk/io-buffer.h>
#include <sync/completion.h>
#include <zircon/listnode.h>
#include <zircon/threads.h>

#include "macros.h"

struct ath10k;
struct ath10k_msg_buf;

/****************/
/* HTC protocol */
/****************/

/*
 * HTC - host-target control protocol
 *
 * tx packets are generally <htc_hdr><payload>
 * rx packets are more complex: <htc_hdr><payload><trailer>
 *
 * The payload + trailer length is stored in len.
 * To get payload-only length one needs to payload - trailer_len.
 *
 * Trailer contains (possibly) multiple <htc_record>.
 * Each record is a id-len-value.
 *
 * HTC header flags, control_byte0, control_byte1
 * have different meaning depending whether its tx
 * or rx.
 *
 * Alignment: htc_hdr, payload and trailer are
 * 4-byte aligned.
 */

#define HTC_HOST_MAX_MSG_PER_BUNDLE        8

enum ath10k_htc_tx_flags {
    ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE = 0x01,
    ATH10K_HTC_FLAG_SEND_BUNDLE        = 0x02
};

enum ath10k_htc_rx_flags {
    ATH10K_HTC_FLAG_TRAILER_PRESENT = 0x02,
    ATH10K_HTC_FLAG_BUNDLE_MASK     = 0xF0
};

struct ath10k_htc_hdr {
    uint8_t eid; /* @enum ath10k_htc_ep_id */
    uint8_t flags; /* @enum ath10k_htc_tx_flags, ath10k_htc_rx_flags */
    uint16_t len;
    union {
        uint8_t trailer_len; /* for rx */
        uint8_t control_byte0;
    } __PACKED;
    union {
        uint8_t seq_no; /* for tx */
        uint8_t control_byte1;
    } __PACKED;
    uint8_t pad0;
    uint8_t pad1;
} __PACKED __ALIGNED(4);

enum ath10k_ath10k_htc_msg_id {
    ATH10K_HTC_MSG_READY_ID                = 1,
    ATH10K_HTC_MSG_CONNECT_SERVICE_ID      = 2,
    ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID = 3,
    ATH10K_HTC_MSG_SETUP_COMPLETE_ID       = 4,
    ATH10K_HTC_MSG_SETUP_COMPLETE_EX_ID    = 5,
    ATH10K_HTC_MSG_SEND_SUSPEND_COMPLETE   = 6
};

enum ath10k_htc_version {
    ATH10K_HTC_VERSION_2P0 = 0x00, /* 2.0 */
    ATH10K_HTC_VERSION_2P1 = 0x01, /* 2.1 */
};

enum ath10k_htc_conn_flags {
    ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_FOURTH    = 0x0,
    ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_HALF      = 0x1,
    ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_THREE_FOURTHS = 0x2,
    ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_UNITY         = 0x3,
#define ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_MASK 0x3
    ATH10K_HTC_CONN_FLAGS_REDUCE_CREDIT_DRIBBLE    = 1 << 2,
    ATH10K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL = 1 << 3
#define ATH10K_HTC_CONN_FLAGS_RECV_ALLOC_MASK 0xFF00
#define ATH10K_HTC_CONN_FLAGS_RECV_ALLOC_LSB  8
};

enum ath10k_htc_conn_svc_status {
    ATH10K_HTC_CONN_SVC_STATUS_SUCCESS      = 0,
    ATH10K_HTC_CONN_SVC_STATUS_NOT_FOUND    = 1,
    ATH10K_HTC_CONN_SVC_STATUS_FAILED       = 2,
    ATH10K_HTC_CONN_SVC_STATUS_NO_RESOURCES = 3,
    ATH10K_HTC_CONN_SVC_STATUS_NO_MORE_EP   = 4
};

enum ath10k_htc_setup_complete_flags {
    ATH10K_HTC_SETUP_COMPLETE_FLAGS_RX_BNDL_EN = 1
};

struct ath10k_ath10k_htc_msg_hdr {
    uint16_t message_id; /* @enum htc_message_id */
} __PACKED;

struct ath10k_htc_unknown {
    uint8_t pad0;
    uint8_t pad1;
} __PACKED;

struct ath10k_htc_ready {
    uint16_t credit_count;
    uint16_t credit_size;
    uint8_t max_endpoints;
    uint8_t pad0;
} __PACKED;

struct ath10k_htc_ready_extended {
    struct ath10k_htc_ready base;
    uint8_t htc_version; /* @enum ath10k_htc_version */
    uint8_t max_msgs_per_htc_bundle;
    uint8_t pad0;
    uint8_t pad1;
} __PACKED;

struct ath10k_htc_conn_svc {
    uint16_t service_id;
    uint16_t flags; /* @enum ath10k_htc_conn_flags */
    uint8_t pad0;
    uint8_t pad1;
} __PACKED;

struct ath10k_htc_conn_svc_response {
    uint16_t service_id;
    uint8_t status; /* @enum ath10k_htc_conn_svc_status */
    uint8_t eid;
    uint16_t max_msg_size;
} __PACKED;

struct ath10k_htc_setup_complete_extended {
    uint8_t pad0;
    uint8_t pad1;
    uint32_t flags; /* @enum htc_setup_complete_flags */
    uint8_t max_msgs_per_bundled_recv;
    uint8_t pad2;
    uint8_t pad3;
    uint8_t pad4;
} __PACKED;

struct ath10k_htc_msg {
    struct ath10k_ath10k_htc_msg_hdr hdr;
    union {
        /* host-to-target */
        struct ath10k_htc_conn_svc connect_service;
        struct ath10k_htc_ready ready;
        struct ath10k_htc_ready_extended ready_ext;
        struct ath10k_htc_unknown unknown;
        struct ath10k_htc_setup_complete_extended setup_complete_ext;

        /* target-to-host */
        struct ath10k_htc_conn_svc_response connect_service_response;
    };
} __PACKED __ALIGNED(4);

enum ath10k_ath10k_htc_record_id {
    ATH10K_HTC_RECORD_NULL             = 0,
    ATH10K_HTC_RECORD_CREDITS          = 1,
    ATH10K_HTC_RECORD_LOOKAHEAD        = 2,
    ATH10K_HTC_RECORD_LOOKAHEAD_BUNDLE = 3,
};

struct ath10k_ath10k_htc_record_hdr {
    uint8_t id; /* @enum ath10k_ath10k_htc_record_id */
    uint8_t len;
    uint8_t pad0;
    uint8_t pad1;
} __PACKED;

struct ath10k_htc_credit_report {
    uint8_t eid; /* @enum ath10k_htc_ep_id */
    uint8_t credits;
    uint8_t pad0;
    uint8_t pad1;
} __PACKED;

struct ath10k_htc_lookahead_report {
    uint8_t pre_valid;
    uint8_t pad0;
    uint8_t pad1;
    uint8_t pad2;
    uint8_t lookahead[4];
    uint8_t post_valid;
    uint8_t pad3;
    uint8_t pad4;
    uint8_t pad5;
} __PACKED;

struct ath10k_htc_lookahead_bundle {
    uint8_t lookahead[4];
} __PACKED;

struct ath10k_htc_record {
    struct ath10k_ath10k_htc_record_hdr hdr;
    union {
        struct ath10k_htc_credit_report credit_report[0];
        struct ath10k_htc_lookahead_report lookahead_report[0];
        struct ath10k_htc_lookahead_bundle lookahead_bundle[0];
        uint8_t pauload[0];
    };
} __PACKED __ALIGNED(4);

/*
 * note: the trailer offset is dynamic depending
 * on payload length. this is only a struct layout draft
 */
struct ath10k_htc_frame {
    struct ath10k_htc_hdr hdr;
    union {
        struct ath10k_htc_msg msg;
        uint8_t payload[0];
    };
    struct ath10k_htc_record trailer[0];
} __PACKED __ALIGNED(4);

/*******************/
/* Host-side stuff */
/*******************/

enum ath10k_htc_svc_gid {
    ATH10K_HTC_SVC_GRP_RSVD = 0,
    ATH10K_HTC_SVC_GRP_WMI = 1,
    ATH10K_HTC_SVC_GRP_NMI = 2,
    ATH10K_HTC_SVC_GRP_HTT = 3,

    ATH10K_HTC_SVC_GRP_TEST = 254,
    ATH10K_HTC_SVC_GRP_LAST = 255,
};

#define SVC(group, idx) \
    (int)(((int)(group) << 8) | (int)(idx))

enum ath10k_htc_svc_id {
    /* NOTE: service ID of 0x0000 is reserved and should never be used */
    ATH10K_HTC_SVC_ID_RESERVED      = 0x0000,
    ATH10K_HTC_SVC_ID_UNUSED        = ATH10K_HTC_SVC_ID_RESERVED,

    ATH10K_HTC_SVC_ID_RSVD_CTRL     = SVC(ATH10K_HTC_SVC_GRP_RSVD, 1),
    ATH10K_HTC_SVC_ID_WMI_CONTROL   = SVC(ATH10K_HTC_SVC_GRP_WMI, 0),
    ATH10K_HTC_SVC_ID_WMI_DATA_BE   = SVC(ATH10K_HTC_SVC_GRP_WMI, 1),
    ATH10K_HTC_SVC_ID_WMI_DATA_BK   = SVC(ATH10K_HTC_SVC_GRP_WMI, 2),
    ATH10K_HTC_SVC_ID_WMI_DATA_VI   = SVC(ATH10K_HTC_SVC_GRP_WMI, 3),
    ATH10K_HTC_SVC_ID_WMI_DATA_VO   = SVC(ATH10K_HTC_SVC_GRP_WMI, 4),

    ATH10K_HTC_SVC_ID_NMI_CONTROL   = SVC(ATH10K_HTC_SVC_GRP_NMI, 0),
    ATH10K_HTC_SVC_ID_NMI_DATA      = SVC(ATH10K_HTC_SVC_GRP_NMI, 1),

    ATH10K_HTC_SVC_ID_HTT_DATA_MSG  = SVC(ATH10K_HTC_SVC_GRP_HTT, 0),

    /* raw stream service (i.e. flash, tcmd, calibration apps) */
    ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS = SVC(ATH10K_HTC_SVC_GRP_TEST, 0),
};

#undef SVC

enum ath10k_htc_ep_id {
    ATH10K_HTC_EP_UNUSED = -1,
    ATH10K_HTC_EP_0 = 0,
    ATH10K_HTC_EP_1 = 1,
    ATH10K_HTC_EP_2,
    ATH10K_HTC_EP_3,
    ATH10K_HTC_EP_4,
    ATH10K_HTC_EP_5,
    ATH10K_HTC_EP_6,
    ATH10K_HTC_EP_7,
    ATH10K_HTC_EP_8,
    ATH10K_HTC_EP_COUNT,
};

struct ath10k_htc_ops {
    void (*target_send_suspend_complete)(struct ath10k* ar);
};

struct ath10k_htc_ep_ops {
    void (*ep_tx_complete)(struct ath10k*, struct ath10k_msg_buf* msg_buf);
    void (*ep_rx_complete)(struct ath10k*, struct ath10k_msg_buf* msg_buf);
    void (*ep_tx_credits)(struct ath10k*);
};

/* service connection information */
struct ath10k_htc_svc_conn_req {
    uint16_t service_id;
    struct ath10k_htc_ep_ops ep_ops;
    int max_send_queue_depth;
};

/* service connection response information */
struct ath10k_htc_svc_conn_resp {
    uint8_t buffer_len;
    uint8_t actual_len;
    enum ath10k_htc_ep_id eid;
    unsigned int max_msg_len;
    uint8_t connect_resp_code;
};

#define ATH10K_NUM_CONTROL_TX_BUFFERS 2
#define ATH10K_HTC_MAX_LEN 4096
#define ATH10K_HTC_MAX_CTRL_MSG_LEN 256
#define ATH10K_HTC_WAIT_TIMEOUT (ZX_SEC(1))
#define ATH10K_HTC_CONTROL_BUFFER_SIZE (ATH10K_HTC_MAX_CTRL_MSG_LEN + \
                                        sizeof(struct ath10k_htc_hdr))

struct ath10k_htc_ep {
    struct ath10k_htc* htc;
    enum ath10k_htc_ep_id eid;
    enum ath10k_htc_svc_id service_id;
    struct ath10k_htc_ep_ops ep_ops;

    int max_tx_queue_depth;
    int max_ep_message_len;
    uint8_t ul_pipe_id;
    uint8_t dl_pipe_id;

    uint8_t seq_no; /* for debugging */
    int tx_credits;
    bool tx_credit_flow_enabled;
};

struct ath10k_htc_svc_tx_credits {
    uint16_t service_id;
    uint8_t  credit_allocation;
};

struct ath10k_htc {
    struct ath10k* ar;
    struct ath10k_htc_ep endpoint[ATH10K_HTC_EP_COUNT];

    /* protects endpoints */
    mtx_t tx_lock;

    struct ath10k_htc_ops htc_ops;

    uint8_t control_resp_buffer[ATH10K_HTC_MAX_CTRL_MSG_LEN];
    int control_resp_len;

    completion_t ctl_resp;

    int total_transmit_credits;
    int target_credit_size;
    uint8_t max_msgs_per_htc_bundle;
};

#define HTC_MSG_PFX(x) ATH10K_MSG_TYPE_HTC_##x

#define HTC_MSG(type, hdr) \
    MSG(HTC_MSG_PFX(type), ATH10K_MSG_TYPE_HTC_MSG, sizeof(struct hdr))

// NB: MSG_TYPE_HTC are used by all messages (HTC, WMI, WMI-TLV, HTT). MSG_TYPE_HTC_MSG,
//     on the other hand, are for messages that are intended for the HTC interface.
#define HTC_MSGS \
    MSG(ATH10K_MSG_TYPE_HTC,     ATH10K_MSG_TYPE_BASE, sizeof(struct ath10k_htc_hdr)), \
    MSG(ATH10K_MSG_TYPE_HTC_MSG, ATH10K_MSG_TYPE_HTC, sizeof(struct ath10k_ath10k_htc_msg_hdr)), \
    HTC_MSG(CONN_SVC,           ath10k_htc_conn_svc),               \
    HTC_MSG(READY,              ath10k_htc_ready),                  \
    HTC_MSG(READY_EXT,          ath10k_htc_ready_extended),         \
    HTC_MSG(UNKNOWN,            ath10k_htc_unknown),                \
    HTC_MSG(SETUP_COMPLETE_EXT, ath10k_htc_setup_complete_extended)

zx_status_t ath10k_htc_init(struct ath10k* ar);
zx_status_t ath10k_htc_wait_target(struct ath10k_htc* htc);
zx_status_t ath10k_htc_start(struct ath10k_htc* htc);
zx_status_t ath10k_htc_connect_service(struct ath10k_htc* htc,
                                       struct ath10k_htc_svc_conn_req*  conn_req,
                                       struct ath10k_htc_svc_conn_resp* conn_resp);
int ath10k_htc_send(struct ath10k_htc* htc, enum ath10k_htc_ep_id eid,
                    struct ath10k_msg_buf* msg_buf);
void ath10k_htc_tx_completion_handler(struct ath10k* ar, struct ath10k_msg_buf* msg_buf);
void ath10k_htc_rx_completion_handler(struct ath10k* ar, struct ath10k_msg_buf* buf);
void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep* ep, struct ath10k_msg_buf* msg_buf);
zx_status_t ath10k_htc_process_trailer(struct ath10k_htc* htc,
                                       uint8_t* buffer,
                                       int length,
                                       enum ath10k_htc_ep_id src_eid,
                                       void* next_lookaheads,
                                       int* next_lookaheads_len);

#endif
