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

/*******************************************************************************
 * Communicates with the dongle by using dcmd codes.
 * For certain dcmd codes, the dongle interprets string data from the host.
 ******************************************************************************/

//#include <linux/netdevice.h>
//#include <linux/types.h>

#include "bcdc.h"

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "fwsignal.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "proto.h"
#include "tracepoint.h"

struct brcmf_proto_bcdc_dcmd {
    uint32_t cmd;       /* dongle command value */
    uint32_t len;       /* lower 16: output buflen;
                         * upper 16: input buflen (excludes header) */
    uint32_t flags;     /* flag defns given below */
    zx_status_t status; /* status code returned from the device */
};

// clang-format off

/* BCDC flag definitions */
#define BCDC_DCMD_ERROR     0x00000001       /* 1=cmd failed */
#define BCDC_DCMD_SET       0x00000002       /* 0=get, 1=set cmd */
#define BCDC_DCMD_IF_MASK   0x0000F000       /* I/F index */
#define BCDC_DCMD_IF_SHIFT  12
#define BCDC_DCMD_ID_MASK   0xFFFF0000       /* id an cmd pairing */
#define BCDC_DCMD_ID_SHIFT  16               /* ID Mask shift bits */
#define BCDC_DCMD_ID(flags) (((flags)&BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT)

/*
 * BCDC header - Broadcom specific extension of CDC.
 * Used on data packets to convey priority across USB.
 */
#define BCDC_HEADER_LEN 4
#define BCDC_PROTO_VER 2          /* Protocol version */
#define BCDC_FLAG_VER_MASK   0xf0   /* Protocol version mask */
#define BCDC_FLAG_VER_SHIFT  4     /* Protocol version shift */
#define BCDC_FLAG_SUM_GOOD   0x04   /* Good RX checksums */
#define BCDC_FLAG_SUM_NEEDED 0x08 /* Dongle needs to do TX checksums */
#define BCDC_PRIORITY_MASK   0x07
#define BCDC_FLAG2_IF_MASK   0x0f /* packet rx interface in APSTA */
#define BCDC_FLAG2_IF_SHIFT  0

// clang-format on

#define BCDC_GET_IF_IDX(hdr) ((int)((((hdr)->flags2) & BCDC_FLAG2_IF_MASK) >> BCDC_FLAG2_IF_SHIFT))
#define BCDC_SET_IF_IDX(hdr, idx) \
    ((hdr)->flags2 = (((hdr)->flags2 & ~BCDC_FLAG2_IF_MASK) | ((idx) << BCDC_FLAG2_IF_SHIFT)))

/**
 * struct brcmf_proto_bcdc_header - BCDC header format
 *
 * @flags: flags contain protocol and checksum info.
 * @priority: 802.1d priority and USB flow control info (bit 4:7).
 * @flags2: additional flags containing dongle interface index.
 * @data_offset: start of packet data. header is following by firmware signals.
 */
struct brcmf_proto_bcdc_header {
    uint8_t flags;
    uint8_t priority;
    uint8_t flags2;
    uint8_t data_offset;
};

/*
 * maximum length of firmware signal data between
 * the BCDC header and packet data in the tx path.
 */
#define BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES 12

#define RETRIES 2 /* # of retries to retrieve matching dcmd response */
/* Must be atleast SDPCM_RESERVE
 * (amount of header tha might be added)
 * plus any space that might be needed
 * for bus alignment padding.
 */
#define BUS_HEADER_LEN (16 + 64)
struct brcmf_bcdc {
    uint16_t reqid;
    uint8_t bus_header[BUS_HEADER_LEN];
    struct brcmf_proto_bcdc_dcmd msg;
    // buf must be packed right after msg; see brcmf_proto_bcdc_msg
    unsigned char buf[BRCMF_DCMD_MAXLEN];
    struct brcmf_fws_info* fws;
};

struct brcmf_fws_info* drvr_to_fws(struct brcmf_pub* drvr) {
    struct brcmf_bcdc* bcdc = drvr->proto->pd;

    return bcdc->fws;
}

static zx_status_t brcmf_proto_bcdc_msg(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                        uint len, bool set) {
    struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
    struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
    uint32_t flags;

    //brcmf_dbg(BCDC, "Enter\n");

    memset(msg, 0, sizeof(struct brcmf_proto_bcdc_dcmd));

    msg->cmd = cmd;
    msg->len = len;
    flags = (++bcdc->reqid << BCDC_DCMD_ID_SHIFT);
    if (set) {
        flags |= BCDC_DCMD_SET;
    }
    flags = (flags & ~BCDC_DCMD_IF_MASK) | (ifidx << BCDC_DCMD_IF_SHIFT);
    msg->flags = flags;

    if (buf) {
        memcpy(bcdc->buf, buf, len);
    }

    len += sizeof(*msg);
    if (len > BRCMF_TX_IOCTL_MAX_MSG_SIZE) {
        len = BRCMF_TX_IOCTL_MAX_MSG_SIZE;
    }

    /* Send request */
    return brcmf_bus_txctl(drvr->bus_if, (unsigned char*)&bcdc->msg, len);
}

static zx_status_t brcmf_proto_bcdc_cmplt(struct brcmf_pub* drvr, uint32_t id, uint32_t len,
                                          int* rxlen_out) {
    zx_status_t ret;
    struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;

    //brcmf_dbg(BCDC, "Enter\n");
    len += sizeof(struct brcmf_proto_bcdc_dcmd);
    do {
        ret = brcmf_bus_rxctl(drvr->bus_if, (unsigned char*)&bcdc->msg, len, rxlen_out);
        if (ret != ZX_OK) {
            break;
        }
    } while (BCDC_DCMD_ID(bcdc->msg.flags) != id);

    return ret;
}

static zx_status_t brcmf_proto_bcdc_query_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd,
                                               void* buf, uint len, zx_status_t* fwerr) {
    struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
    struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
    void* info;
    zx_status_t ret = ZX_OK;
    int retries = 0;
    int rxlen;
    uint32_t id, flags;

    //brcmf_dbg(BCDC, "Enter, cmd %d len %d\n", cmd, len);

    *fwerr = ZX_OK;
    ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, false);
    if (ret != ZX_OK) {
        brcmf_err("brcmf_proto_bcdc_msg failed w/status %d\n", ret);
        goto done;
    }

retry:
    /* wait for interrupt and get first fragment */
    ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len, &rxlen);
    if (ret != ZX_OK) {
        goto done;
    }

    flags = msg->flags;
    id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

    if ((id < bcdc->reqid) && (++retries < RETRIES)) {
        goto retry;
    }
    if (id != bcdc->reqid) {
        brcmf_err("%s: unexpected request id %d (expected %d)\n",
                  brcmf_ifname(brcmf_get_ifp(drvr, ifidx)), id, bcdc->reqid);
        ret = ZX_ERR_BAD_STATE;
        goto done;
    }

    /* Check info buffer */
    info = (void*)&bcdc->buf[0];

    /* Copy info buffer */
    if (buf) {
        if (rxlen < (int)len) {
            len = rxlen;
        }
        memcpy(buf, info, len);
    }

    ret = ZX_OK;

    /* Check the ERROR flag */
    if (flags & BCDC_DCMD_ERROR) {
        *fwerr = msg->status;
    }
done:
    return ret;
}

static zx_status_t brcmf_proto_bcdc_set_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                             uint len, zx_status_t* fwerr) {
    struct brcmf_bcdc* bcdc = (struct brcmf_bcdc*)drvr->proto->pd;
    struct brcmf_proto_bcdc_dcmd* msg = &bcdc->msg;
    zx_status_t ret;
    uint32_t flags, id;
    int rxlen_out;

    //brcmf_dbg(BCDC, "Enter, cmd %d len %d\n", cmd, len);

    *fwerr = ZX_OK;
    ret = brcmf_proto_bcdc_msg(drvr, ifidx, cmd, buf, len, true);
    if (ret != ZX_OK) {
        goto done;
    }

    ret = brcmf_proto_bcdc_cmplt(drvr, bcdc->reqid, len, &rxlen_out);
    if (ret != ZX_OK) {
        goto done;
    }

    flags = msg->flags;
    id = (flags & BCDC_DCMD_ID_MASK) >> BCDC_DCMD_ID_SHIFT;

    if (id != bcdc->reqid) {
        brcmf_err("%s: unexpected request id %d (expected %d)\n",
                  brcmf_ifname(brcmf_get_ifp(drvr, ifidx)), id, bcdc->reqid);
        ret = ZX_ERR_BAD_STATE;
        goto done;
    }

    ret = ZX_OK;

    /* Check the ERROR flag */
    if (flags & BCDC_DCMD_ERROR) {
        *fwerr = msg->status;
    }

done:
    return ret;
}

static void brcmf_proto_bcdc_hdrpush(struct brcmf_pub* drvr, int ifidx, uint8_t offset,
                                     struct brcmf_netbuf* pktbuf) {
    struct brcmf_proto_bcdc_header* h;

    brcmf_dbg(BCDC, "Enter\n");

    /* Push BDC header used to convey priority for buses that don't */
    brcmf_netbuf_grow_head(pktbuf, BCDC_HEADER_LEN);

    h = (struct brcmf_proto_bcdc_header*)(pktbuf->data);

    h->flags = (BCDC_PROTO_VER << BCDC_FLAG_VER_SHIFT);
    if (pktbuf->ip_summed == CHECKSUM_PARTIAL) {
        h->flags |= BCDC_FLAG_SUM_NEEDED;
    }

    h->priority = (pktbuf->priority & BCDC_PRIORITY_MASK);
    h->flags2 = 0;
    h->data_offset = offset;
    BCDC_SET_IF_IDX(h, ifidx);
    // trace_brcmf_bcdchdr(pktbuf->data);
}

static zx_status_t brcmf_proto_bcdc_hdrpull(struct brcmf_pub* drvr, bool do_fws,
                                            struct brcmf_netbuf* pktbuf,
                                            struct brcmf_if** ifp) {
    struct brcmf_proto_bcdc_header* h;
    struct brcmf_if* tmp_if;

    brcmf_dbg(BCDC, "Enter\n");

    /* Pop BCDC header used to convey priority for buses that don't */
    if (pktbuf->len <= BCDC_HEADER_LEN) {
        brcmf_dbg(INFO, "rx data too short (%d <= %d)\n", pktbuf->len, BCDC_HEADER_LEN);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    // trace_brcmf_bcdchdr(pktbuf->data);
    h = (struct brcmf_proto_bcdc_header*)(pktbuf->data);

    tmp_if = brcmf_get_ifp(drvr, BCDC_GET_IF_IDX(h));
    if (!tmp_if) {
        brcmf_dbg(INFO, "no matching ifp found\n");
        return ZX_ERR_NOT_FOUND;
    }
    if (((h->flags & BCDC_FLAG_VER_MASK) >> BCDC_FLAG_VER_SHIFT) != BCDC_PROTO_VER) {
        brcmf_err("%s: non-BCDC packet received, flags 0x%x\n", brcmf_ifname(tmp_if), h->flags);
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    if (h->flags & BCDC_FLAG_SUM_GOOD) {
        brcmf_dbg(BCDC, "%s: BDC rcv, good checksum, flags 0x%x\n", brcmf_ifname(tmp_if), h->flags);
        pktbuf->ip_summed = CHECKSUM_UNNECESSARY;
    }

    pktbuf->priority = h->priority & BCDC_PRIORITY_MASK;

    brcmf_netbuf_shrink_head(pktbuf, BCDC_HEADER_LEN);
    if (do_fws) {
        brcmf_fws_hdrpull(tmp_if, h->data_offset << 2, pktbuf);
    } else {
        brcmf_netbuf_shrink_head(pktbuf, h->data_offset << 2);
    }

    if (pktbuf->len == 0) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    if (ifp != NULL) {
        *ifp = tmp_if;
    }
    return ZX_OK;
}

static zx_status_t brcmf_proto_bcdc_tx_queue_data(struct brcmf_pub* drvr, int ifidx,
                                                  struct brcmf_netbuf* netbuf) {
    struct brcmf_if* ifp = brcmf_get_ifp(drvr, ifidx);
    struct brcmf_bcdc* bcdc = drvr->proto->pd;

    if (!brcmf_fws_queue_skbs(bcdc->fws)) {
        return brcmf_proto_txdata(drvr, ifidx, 0, netbuf);
    }

    return brcmf_fws_process_skb(ifp, netbuf);
}

static int brcmf_proto_bcdc_txdata(struct brcmf_pub* drvr, int ifidx, uint8_t offset,
                                   struct brcmf_netbuf* pktbuf) {
    brcmf_proto_bcdc_hdrpush(drvr, ifidx, offset, pktbuf);
    return brcmf_bus_txdata(drvr->bus_if, pktbuf);
}

void brcmf_proto_bcdc_txflowblock(struct brcmf_device* dev, bool state) {
    struct brcmf_bus* bus_if = dev_get_drvdata(dev);
    struct brcmf_pub* drvr = bus_if->drvr;

    brcmf_dbg(TRACE, "Enter\n");

    brcmf_fws_bus_blocked(drvr, state);
}

void brcmf_proto_bcdc_txcomplete(struct brcmf_device* dev, struct brcmf_netbuf* txp, bool success) {
    struct brcmf_bus* bus_if = dev_get_drvdata(dev);
    struct brcmf_bcdc* bcdc = bus_if->drvr->proto->pd;
    struct brcmf_if* ifp;

    /* await txstatus signal for firmware if active */
    if (brcmf_fws_fc_active(bcdc->fws)) {
        if (!success) {
            brcmf_fws_bustxfail(bcdc->fws, txp);
        }
    } else {
        if (brcmf_proto_bcdc_hdrpull(bus_if->drvr, false, txp, &ifp)) {
            brcmu_pkt_buf_free_skb(txp);
        } else {
            brcmf_txfinalize(ifp, txp, success);
        }
    }
}

static void brcmf_proto_bcdc_configure_addr_mode(struct brcmf_pub* drvr, int ifidx,
                                                 enum proto_addr_mode addr_mode) {}

static void brcmf_proto_bcdc_delete_peer(struct brcmf_pub* drvr, int ifidx,
                                         uint8_t peer[ETH_ALEN]) {}

static void brcmf_proto_bcdc_add_tdls_peer(struct brcmf_pub* drvr, int ifidx,
                                           uint8_t peer[ETH_ALEN]) {}

static void brcmf_proto_bcdc_rxreorder(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {
    brcmf_fws_rxreorder(ifp, netbuf);
}

static void brcmf_proto_bcdc_add_if(struct brcmf_if* ifp) {
    brcmf_fws_add_interface(ifp);
}

static void brcmf_proto_bcdc_del_if(struct brcmf_if* ifp) {
    brcmf_fws_del_interface(ifp);
}

static void brcmf_proto_bcdc_reset_if(struct brcmf_if* ifp) {
    brcmf_fws_reset_interface(ifp);
}

static zx_status_t brcmf_proto_bcdc_init_done(struct brcmf_pub* drvr) {
    struct brcmf_bcdc* bcdc = drvr->proto->pd;
    struct brcmf_fws_info* fws;
    zx_status_t err;

    err = brcmf_fws_attach(drvr, &fws);
    if (err != ZX_OK) {
        bcdc->fws = NULL;
        return err;
    }

    bcdc->fws = fws;
    return ZX_OK;
}

zx_status_t brcmf_proto_bcdc_attach(struct brcmf_pub* drvr) {
    struct brcmf_bcdc* bcdc;

    bcdc = calloc(1, sizeof(*bcdc));
    if (!bcdc) {
        goto fail;
    }

    /* ensure that the msg buf directly follows the cdc msg struct */
    if ((unsigned long)(&bcdc->msg + 1) != (unsigned long)bcdc->buf) {
        brcmf_err("struct brcmf_proto_bcdc is not correctly defined\n");
        goto fail;
    }

    drvr->proto->hdrpull = brcmf_proto_bcdc_hdrpull;
    drvr->proto->query_dcmd = brcmf_proto_bcdc_query_dcmd;
    drvr->proto->set_dcmd = brcmf_proto_bcdc_set_dcmd;
    drvr->proto->tx_queue_data = brcmf_proto_bcdc_tx_queue_data;
    drvr->proto->txdata = brcmf_proto_bcdc_txdata;
    drvr->proto->configure_addr_mode = brcmf_proto_bcdc_configure_addr_mode;
    drvr->proto->delete_peer = brcmf_proto_bcdc_delete_peer;
    drvr->proto->add_tdls_peer = brcmf_proto_bcdc_add_tdls_peer;
    drvr->proto->rxreorder = brcmf_proto_bcdc_rxreorder;
    drvr->proto->add_if = brcmf_proto_bcdc_add_if;
    drvr->proto->del_if = brcmf_proto_bcdc_del_if;
    drvr->proto->reset_if = brcmf_proto_bcdc_reset_if;
    drvr->proto->init_done = brcmf_proto_bcdc_init_done;
    drvr->proto->pd = bcdc;

    drvr->hdrlen += BCDC_HEADER_LEN + BRCMF_PROT_FW_SIGNAL_MAX_TXBYTES;
    drvr->bus_if->maxctl = BRCMF_DCMD_MAXLEN + sizeof(struct brcmf_proto_bcdc_dcmd);
    return ZX_OK;

fail:
    free(bcdc);
    return ZX_ERR_NO_MEMORY;
}

void brcmf_proto_bcdc_detach(struct brcmf_pub* drvr) {
    struct brcmf_bcdc* bcdc = drvr->proto->pd;

    drvr->proto->pd = NULL;
    brcmf_fws_detach(bcdc->fws);
    free(bcdc);
}
