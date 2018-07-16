/* Copyright (c) 2014 Broadcom Corporation
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

#include "msgbuf.h"

#include <stdatomic.h>
#include <threads.h>

#include <sync/completion.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "bus.h"
#include "commonring.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "flowring.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "proto.h"
#include "tracepoint.h"
#include "workqueue.h"

#define MSGBUF_IOCTL_RESP_TIMEOUT_MSEC (2000)

#define MSGBUF_TYPE_GEN_STATUS 0x1
#define MSGBUF_TYPE_RING_STATUS 0x2
#define MSGBUF_TYPE_FLOW_RING_CREATE 0x3
#define MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT 0x4
#define MSGBUF_TYPE_FLOW_RING_DELETE 0x5
#define MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT 0x6
#define MSGBUF_TYPE_FLOW_RING_FLUSH 0x7
#define MSGBUF_TYPE_FLOW_RING_FLUSH_CMPLT 0x8
#define MSGBUF_TYPE_IOCTLPTR_REQ 0x9
#define MSGBUF_TYPE_IOCTLPTR_REQ_ACK 0xA
#define MSGBUF_TYPE_IOCTLRESP_BUF_POST 0xB
#define MSGBUF_TYPE_IOCTL_CMPLT 0xC
#define MSGBUF_TYPE_EVENT_BUF_POST 0xD
#define MSGBUF_TYPE_WL_EVENT 0xE
#define MSGBUF_TYPE_TX_POST 0xF
#define MSGBUF_TYPE_TX_STATUS 0x10
#define MSGBUF_TYPE_RXBUF_POST 0x11
#define MSGBUF_TYPE_RX_CMPLT 0x12
#define MSGBUF_TYPE_LPBK_DMAXFER 0x13
#define MSGBUF_TYPE_LPBK_DMAXFER_CMPLT 0x14

#define NR_TX_PKTIDS 2048
#define NR_RX_PKTIDS 1024

#define BRCMF_IOCTL_REQ_PKTID 0xFFFE

#define BRCMF_MSGBUF_MAX_PKT_SIZE 2048
#define BRCMF_MSGBUF_RXBUFPOST_THRESHOLD 32
#define BRCMF_MSGBUF_MAX_IOCTLRESPBUF_POST 8
#define BRCMF_MSGBUF_MAX_EVENTBUF_POST 8

#define BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3 0x01
#define BRCMF_MSGBUF_PKT_FLAGS_PRIO_SHIFT 5

#define BRCMF_MSGBUF_TX_FLUSH_CNT1 32
#define BRCMF_MSGBUF_TX_FLUSH_CNT2 96

#define BRCMF_MSGBUF_DELAY_TXWORKER_THRS 96
#define BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS 32
#define BRCMF_MSGBUF_UPDATE_RX_PTR_THRS 48

struct msgbuf_common_hdr {
    uint8_t msgtype;
    uint8_t ifidx;
    uint8_t flags;
    uint8_t rsvd0;
    uint32_t request_id;
};

struct msgbuf_ioctl_req_hdr {
    struct msgbuf_common_hdr msg;
    uint32_t cmd;
    uint16_t trans_id;
    uint16_t input_buf_len;
    uint16_t output_buf_len;
    uint16_t rsvd0[3];
    struct msgbuf_buf_addr req_buf_addr;
    uint32_t rsvd1[2];
};

struct msgbuf_tx_msghdr {
    struct msgbuf_common_hdr msg;
    uint8_t txhdr[ETH_HLEN];
    uint8_t flags;
    uint8_t seg_cnt;
    struct msgbuf_buf_addr metadata_buf_addr;
    struct msgbuf_buf_addr data_buf_addr;
    uint16_t metadata_buf_len;
    uint16_t data_len;
    uint32_t rsvd0;
};

struct msgbuf_rx_bufpost {
    struct msgbuf_common_hdr msg;
    uint16_t metadata_buf_len;
    uint16_t data_buf_len;
    uint32_t rsvd0;
    struct msgbuf_buf_addr metadata_buf_addr;
    struct msgbuf_buf_addr data_buf_addr;
};

struct msgbuf_rx_ioctl_resp_or_event {
    struct msgbuf_common_hdr msg;
    uint16_t host_buf_len;
    uint16_t rsvd0[3];
    struct msgbuf_buf_addr host_buf_addr;
    uint32_t rsvd1[4];
};

struct msgbuf_completion_hdr {
    uint16_t status;
    uint16_t flow_ring_id;
};

struct msgbuf_rx_event {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint16_t event_data_len;
    uint16_t seqnum;
    uint16_t rsvd0[4];
};

struct msgbuf_ioctl_resp_hdr {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint16_t resp_len;
    uint16_t trans_id;
    uint32_t cmd;
    uint32_t rsvd0;
};

struct msgbuf_tx_status {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint16_t metadata_len;
    uint16_t tx_status;
};

struct msgbuf_rx_complete {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint16_t metadata_len;
    uint16_t data_len;
    uint16_t data_offset;
    uint16_t flags;
    uint32_t rx_status_0;
    uint32_t rx_status_1;
    uint32_t rsvd0;
};

struct msgbuf_tx_flowring_create_req {
    struct msgbuf_common_hdr msg;
    uint8_t da[ETH_ALEN];
    uint8_t sa[ETH_ALEN];
    uint8_t tid;
    uint8_t if_flags;
    uint16_t flow_ring_id;
    uint8_t tc;
    uint8_t priority;
    uint16_t int_vector;
    uint16_t max_items;
    uint16_t len_item;
    struct msgbuf_buf_addr flow_ring_addr;
};

struct msgbuf_tx_flowring_delete_req {
    struct msgbuf_common_hdr msg;
    uint16_t flow_ring_id;
    uint16_t reason;
    uint32_t rsvd0[7];
};

struct msgbuf_flowring_create_resp {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint32_t rsvd0[3];
};

struct msgbuf_flowring_delete_resp {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint32_t rsvd0[3];
};

struct msgbuf_flowring_flush_resp {
    struct msgbuf_common_hdr msg;
    struct msgbuf_completion_hdr compl_hdr;
    uint32_t rsvd0[3];
};

struct brcmf_msgbuf_work_item {
    struct list_node queue;
    uint32_t flowid;
    int ifidx;
    uint8_t sa[ETH_ALEN];
    uint8_t da[ETH_ALEN];
};

struct brcmf_msgbuf {
    struct brcmf_pub* drvr;

    struct brcmf_commonring** commonrings;
    struct brcmf_commonring** flowrings;
    dma_addr_t* flowring_dma_handle;

    uint16_t max_flowrings;
    uint16_t max_submissionrings;
    uint16_t max_completionrings;

    uint16_t rx_dataoffset;
    uint32_t max_rxbufpost;
    uint16_t rx_metadata_offset;
    uint32_t rxbufpost;

    uint32_t max_ioctlrespbuf;
    uint32_t cur_ioctlrespbuf;
    uint32_t max_eventbuf;
    uint32_t cur_eventbuf;

    void* ioctbuf;
    dma_addr_t ioctbuf_handle;
    uint32_t ioctbuf_phys_hi;
    uint32_t ioctbuf_phys_lo;
    zx_status_t ioctl_resp_status;
    uint32_t ioctl_resp_ret_len;
    uint32_t ioctl_resp_pktid;

    uint16_t data_seq_no;
    uint16_t ioctl_seq_no;
    uint32_t reqid;
    completion_t ioctl_resp_wait;

    struct brcmf_msgbuf_pktids* tx_pktids;
    struct brcmf_msgbuf_pktids* rx_pktids;
    struct brcmf_flowring* flow;

    struct workqueue_struct* txflow_wq;
    struct work_struct txflow_work;
    atomic_ulong* flow_map;
    atomic_ulong* txstatus_done_map;

    struct work_struct flowring_work;
    //spinlock_t flowring_work_lock;
    struct list_node work_queue;
};

struct brcmf_msgbuf_pktid {
    atomic_int allocated;
    uint16_t data_offset;
    struct brcmf_netbuf* netbuf;
    dma_addr_t physaddr;
};

struct brcmf_msgbuf_pktids {
    uint32_t array_size;
    uint32_t last_allocated_idx;
    enum dma_data_direction direction;
    struct brcmf_msgbuf_pktid* array;
};

static void brcmf_msgbuf_rxbuf_ioctlresp_post(struct brcmf_msgbuf* msgbuf);

static struct brcmf_msgbuf_pktids* brcmf_msgbuf_init_pktids(uint32_t nr_array_entries,
                                                            enum dma_data_direction direction) {
    struct brcmf_msgbuf_pktid* array;
    struct brcmf_msgbuf_pktids* pktids;

    array = calloc(nr_array_entries, sizeof(*array));
    if (!array) {
        return NULL;
    }

    pktids = calloc(1, sizeof(*pktids));
    if (!pktids) {
        free(array);
        return NULL;
    }
    pktids->array = array;
    pktids->array_size = nr_array_entries;

    return pktids;
}

static zx_status_t brcmf_msgbuf_alloc_pktid(struct brcmf_device* dev,
                                            struct brcmf_msgbuf_pktids* pktids,
                                            struct brcmf_netbuf* netbuf, uint16_t data_offset,
                                            dma_addr_t* physaddr, uint32_t* idx) {
    struct brcmf_msgbuf_pktid* array;
    uint32_t count;

    array = pktids->array;

    *physaddr =
        dma_map_single(dev, netbuf->data + data_offset, netbuf->len - data_offset,
                       pktids->direction);

    if (dma_mapping_error(dev, *physaddr)) {
        brcmf_err("dma_map_single failed !!\n");
        return ZX_ERR_NO_MEMORY;
    }

    *idx = pktids->last_allocated_idx;

    count = 0;
    do {
        (*idx)++;
        if (*idx == pktids->array_size) {
            *idx = 0;
        }
        if (atomic_load(&array[*idx].allocated) == 0) {
            int expected = 0;
            atomic_compare_exchange_strong(&array[*idx].allocated, &expected, 1);
            if (expected == 0) {
                break;
            }
        }
        count++;
    } while (count < pktids->array_size);

    if (count == pktids->array_size) {
        return ZX_ERR_NO_MEMORY;
    }

    array[*idx].data_offset = data_offset;
    array[*idx].physaddr = *physaddr;
    array[*idx].netbuf = netbuf;

    pktids->last_allocated_idx = *idx;

    return ZX_OK;
}

static struct brcmf_netbuf* brcmf_msgbuf_get_pktid(struct brcmf_device* dev,
                                              struct brcmf_msgbuf_pktids* pktids, uint32_t idx) {
    struct brcmf_msgbuf_pktid* pktid;
    struct brcmf_netbuf* netbuf;

    if (idx >= pktids->array_size) {
        brcmf_err("Invalid packet id %d (max %d)\n", idx, pktids->array_size);
        return NULL;
    }
    if (atomic_load(&pktids->array[idx].allocated)) {
        pktid = &pktids->array[idx];
        dma_unmap_single(dev, pktid->physaddr, pktid->netbuf->len - pktid->data_offset,
                         pktids->direction);
        netbuf = pktid->netbuf;
        atomic_store(&pktid->allocated, 0);
        return netbuf;
    } else {
        brcmf_err("Invalid packet id %d (not in use)\n", idx);
    }

    return NULL;
}

static void brcmf_msgbuf_release_array(struct brcmf_device* dev,
                                       struct brcmf_msgbuf_pktids* pktids) {
    struct brcmf_msgbuf_pktid* array;
    struct brcmf_msgbuf_pktid* pktid;
    uint32_t count;

    array = pktids->array;
    count = 0;
    do {
        if (atomic_load(&array[count].allocated)) {
            pktid = &array[count];
            dma_unmap_single(dev, pktid->physaddr, pktid->netbuf->len - pktid->data_offset,
                             pktids->direction);
            brcmu_pkt_buf_free_netbuf(pktid->netbuf);
        }
        count++;
    } while (count < pktids->array_size);

    free(array);
    free(pktids);
}

static void brcmf_msgbuf_release_pktids(struct brcmf_msgbuf* msgbuf) {
    if (msgbuf->rx_pktids) {
        brcmf_msgbuf_release_array(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids);
    }
    if (msgbuf->tx_pktids) {
        brcmf_msgbuf_release_array(msgbuf->drvr->bus_if->dev, msgbuf->tx_pktids);
    }
}

static zx_status_t brcmf_msgbuf_tx_ioctl(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                         uint len) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct brcmf_commonring* commonring;
    struct msgbuf_ioctl_req_hdr* request;
    uint16_t buf_len;
    void* ret_ptr;
    zx_status_t err;

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
    brcmf_commonring_lock(commonring);
    ret_ptr = brcmf_commonring_reserve_for_write(commonring);
    if (!ret_ptr) {
        brcmf_err("Failed to reserve space in commonring\n");
        brcmf_commonring_unlock(commonring);
        return ZX_ERR_NO_MEMORY;
    }

    msgbuf->reqid++;

    request = (struct msgbuf_ioctl_req_hdr*)ret_ptr;
    request->msg.msgtype = MSGBUF_TYPE_IOCTLPTR_REQ;
    request->msg.ifidx = (uint8_t)ifidx;
    request->msg.flags = 0;
    request->msg.request_id = BRCMF_IOCTL_REQ_PKTID;
    request->cmd = cmd;
    request->output_buf_len = len;
    request->trans_id = msgbuf->reqid;

    buf_len = min_t(uint16_t, len, BRCMF_TX_IOCTL_MAX_MSG_SIZE);
    request->input_buf_len = buf_len;
    request->req_buf_addr.high_addr = msgbuf->ioctbuf_phys_hi;
    request->req_buf_addr.low_addr = msgbuf->ioctbuf_phys_lo;
    if (buf) {
        memcpy(msgbuf->ioctbuf, buf, buf_len);
    } else {
        memset(msgbuf->ioctbuf, 0, buf_len);
    }

    err = brcmf_commonring_write_complete(commonring);
    brcmf_commonring_unlock(commonring);

    return err;
}

static zx_status_t brcmf_msgbuf_ioctl_resp_wait(struct brcmf_msgbuf* msgbuf) {
    return completion_wait(&msgbuf->ioctl_resp_wait, ZX_MSEC(MSGBUF_IOCTL_RESP_TIMEOUT_MSEC));
}

static void brcmf_msgbuf_ioctl_resp_wake(struct brcmf_msgbuf* msgbuf) {
    completion_signal(&msgbuf->ioctl_resp_wait);
}

static zx_status_t brcmf_msgbuf_query_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                           uint len, zx_status_t* fwerr) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct brcmf_netbuf* netbuf = NULL;
    zx_status_t err;

    brcmf_dbg(MSGBUF, "ifidx=%d, cmd=%d, len=%d\n", ifidx, cmd, len);
    *fwerr = ZX_OK;
    completion_reset(&msgbuf->ioctl_resp_wait);
    err = brcmf_msgbuf_tx_ioctl(drvr, ifidx, cmd, buf, len);
    if (err != ZX_OK) {
        return err;
    }

    err = brcmf_msgbuf_ioctl_resp_wait(msgbuf);
    if (err != ZX_OK) {
        brcmf_err("Timeout on response for query command\n");
        return ZX_ERR_IO;
    }

    netbuf = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids,
                                 msgbuf->ioctl_resp_pktid);
    if (msgbuf->ioctl_resp_ret_len != 0) {
        if (!netbuf) {
            return ZX_ERR_NOT_FOUND;
        }

        memcpy(buf, netbuf->data,
               (len < msgbuf->ioctl_resp_ret_len) ? len : msgbuf->ioctl_resp_ret_len);
    }
    brcmu_pkt_buf_free_netbuf(netbuf);

    *fwerr = msgbuf->ioctl_resp_status;
    return ZX_OK;
}

static zx_status_t brcmf_msgbuf_set_dcmd(struct brcmf_pub* drvr, int ifidx, uint cmd, void* buf,
                                         uint len, zx_status_t* fwerr) {
    return brcmf_msgbuf_query_dcmd(drvr, ifidx, cmd, buf, len, fwerr);
}

static zx_status_t brcmf_msgbuf_hdrpull(struct brcmf_pub* drvr, bool do_fws,
                                        struct brcmf_netbuf* netbuf, struct brcmf_if** ifp) {
    return ZX_ERR_IO_NOT_PRESENT;
}

static void brcmf_msgbuf_rxreorder(struct brcmf_if* ifp, struct brcmf_netbuf* netbuf) {}

static void brcmf_msgbuf_remove_flowring(struct brcmf_msgbuf* msgbuf, uint16_t flowid) {
    uint32_t dma_sz;
    void* dma_buf;

    brcmf_dbg(MSGBUF, "Removing flowring %d\n", flowid);

    dma_sz = BRCMF_H2D_TXFLOWRING_MAX_ITEM * BRCMF_H2D_TXFLOWRING_ITEMSIZE;
    dma_buf = msgbuf->flowrings[flowid]->buf_addr;
    dma_free_coherent(msgbuf->drvr->bus_if->dev, dma_sz, dma_buf,
                      msgbuf->flowring_dma_handle[flowid]);

    brcmf_flowring_delete(msgbuf->flow, flowid);
}

static struct brcmf_msgbuf_work_item* brcmf_msgbuf_dequeue_work(struct brcmf_msgbuf* msgbuf) {
    struct brcmf_msgbuf_work_item* work = NULL;

    //spin_lock_irqsave(&msgbuf->flowring_work_lock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    if (!list_is_empty(&msgbuf->work_queue)) {
        work = list_peek_head_type(&msgbuf->work_queue, struct brcmf_msgbuf_work_item, queue);
        list_delete(&work->queue);
    }
    //spin_unlock_irqrestore(&msgbuf->flowring_work_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);

    return work;
}

static uint32_t brcmf_msgbuf_flowring_create_worker(struct brcmf_msgbuf* msgbuf,
                                                    struct brcmf_msgbuf_work_item* work) {
    struct msgbuf_tx_flowring_create_req* create;
    struct brcmf_commonring* commonring;
    void* ret_ptr;
    uint32_t flowid;
    void* dma_buf;
    uint32_t dma_sz;
    uint64_t address;
    zx_status_t err;

    flowid = work->flowid;
    dma_sz = BRCMF_H2D_TXFLOWRING_MAX_ITEM * BRCMF_H2D_TXFLOWRING_ITEMSIZE;
    dma_buf = dma_alloc_coherent(msgbuf->drvr->bus_if->dev, dma_sz,
                                 &msgbuf->flowring_dma_handle[flowid], GFP_KERNEL);
    if (!dma_buf) {
        brcmf_err("dma_alloc_coherent failed\n");
        brcmf_flowring_delete(msgbuf->flow, flowid);
        return BRCMF_FLOWRING_INVALID_ID;
    }

    brcmf_commonring_config(msgbuf->flowrings[flowid], BRCMF_H2D_TXFLOWRING_MAX_ITEM,
                            BRCMF_H2D_TXFLOWRING_ITEMSIZE, dma_buf);

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
    brcmf_commonring_lock(commonring);
    ret_ptr = brcmf_commonring_reserve_for_write(commonring);
    if (!ret_ptr) {
        brcmf_err("Failed to reserve space in commonring\n");
        brcmf_commonring_unlock(commonring);
        brcmf_msgbuf_remove_flowring(msgbuf, flowid);
        return BRCMF_FLOWRING_INVALID_ID;
    }

    create = (struct msgbuf_tx_flowring_create_req*)ret_ptr;
    create->msg.msgtype = MSGBUF_TYPE_FLOW_RING_CREATE;
    create->msg.ifidx = work->ifidx;
    create->msg.request_id = 0;
    create->tid = brcmf_flowring_tid(msgbuf->flow, flowid);
    create->flow_ring_id = flowid + BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
    memcpy(create->sa, work->sa, ETH_ALEN);
    memcpy(create->da, work->da, ETH_ALEN);
    address = (uint64_t)msgbuf->flowring_dma_handle[flowid];
    create->flow_ring_addr.high_addr = address >> 32;
    create->flow_ring_addr.low_addr = address & 0xffffffff;
    create->max_items = BRCMF_H2D_TXFLOWRING_MAX_ITEM;
    create->len_item = BRCMF_H2D_TXFLOWRING_ITEMSIZE;

    brcmf_dbg(MSGBUF, "Send Flow Create Req flow ID %d for peer %pM prio %d ifindex %d\n", flowid,
              work->da, create->tid, work->ifidx);

    err = brcmf_commonring_write_complete(commonring);
    brcmf_commonring_unlock(commonring);
    if (err != ZX_OK) {
        brcmf_err("Failed to write commonring\n");
        brcmf_msgbuf_remove_flowring(msgbuf, flowid);
        return BRCMF_FLOWRING_INVALID_ID;
    }

    return flowid;
}

static void brcmf_msgbuf_flowring_worker(struct work_struct* work) {
    struct brcmf_msgbuf* msgbuf;
    struct brcmf_msgbuf_work_item* create;

    msgbuf = containerof(work, struct brcmf_msgbuf, flowring_work);

    while ((create = brcmf_msgbuf_dequeue_work(msgbuf))) {
        brcmf_msgbuf_flowring_create_worker(msgbuf, create);
        free(create);
    }
}

static uint32_t brcmf_msgbuf_flowring_create(struct brcmf_msgbuf* msgbuf, int ifidx,
                                             struct brcmf_netbuf* netbuf) {
    struct brcmf_msgbuf_work_item* create;
    struct ethhdr* eh = (struct ethhdr*)(netbuf->data);
    uint32_t flowid;

    create = calloc(1, sizeof(*create));
    if (create == NULL) {
        return BRCMF_FLOWRING_INVALID_ID;
    }

    flowid = brcmf_flowring_create(msgbuf->flow, eh->h_dest, netbuf->priority, ifidx);
    if (flowid == BRCMF_FLOWRING_INVALID_ID) {
        free(create);
        return flowid;
    }

    create->flowid = flowid;
    create->ifidx = ifidx;
    memcpy(create->sa, eh->h_source, ETH_ALEN);
    memcpy(create->da, eh->h_dest, ETH_ALEN);

    //spin_lock_irqsave(&msgbuf->flowring_work_lock, flags);
    pthread_mutex_lock(&irq_callback_lock);
    list_add_tail(&msgbuf->work_queue, &create->queue);
    //spin_unlock_irqrestore(&msgbuf->flowring_work_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
    workqueue_schedule_default(&msgbuf->flowring_work);

    return flowid;
}

static void brcmf_msgbuf_txflow(struct brcmf_msgbuf* msgbuf, uint16_t flowid) {
    struct brcmf_flowring* flow = msgbuf->flow;
    struct brcmf_commonring* commonring;
    void* ret_ptr;
    uint32_t count;
    struct brcmf_netbuf* netbuf;
    dma_addr_t physaddr;
    uint32_t pktid;
    struct msgbuf_tx_msghdr* tx_msghdr;
    uint64_t address;

    commonring = msgbuf->flowrings[flowid];
    if (!brcmf_commonring_write_available(commonring)) {
        return;
    }

    brcmf_commonring_lock(commonring);

    count = BRCMF_MSGBUF_TX_FLUSH_CNT2 - BRCMF_MSGBUF_TX_FLUSH_CNT1;
    while (brcmf_flowring_qlen(flow, flowid)) {
        netbuf = brcmf_flowring_dequeue(flow, flowid);
        if (netbuf == NULL) {
            brcmf_err("No netbuf, but qlen %d\n", brcmf_flowring_qlen(flow, flowid));
            break;
        }
        if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev, msgbuf->tx_pktids, netbuf, ETH_HLEN,
                                     &physaddr, &pktid)) {
            brcmf_flowring_reinsert(flow, flowid, netbuf);
            brcmf_err("No PKTID available !!\n");
            break;
        }
        ret_ptr = brcmf_commonring_reserve_for_write(commonring);
        if (!ret_ptr) {
            brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev, msgbuf->tx_pktids, pktid);
            brcmf_flowring_reinsert(flow, flowid, netbuf);
            break;
        }
        count++;

        tx_msghdr = (struct msgbuf_tx_msghdr*)ret_ptr;

        tx_msghdr->msg.msgtype = MSGBUF_TYPE_TX_POST;
        tx_msghdr->msg.request_id = pktid;
        tx_msghdr->msg.ifidx = brcmf_flowring_ifidx_get(flow, flowid);
        tx_msghdr->flags = BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3;
        tx_msghdr->flags |= (netbuf->priority & 0x07) << BRCMF_MSGBUF_PKT_FLAGS_PRIO_SHIFT;
        tx_msghdr->seg_cnt = 1;
        memcpy(tx_msghdr->txhdr, netbuf->data, ETH_HLEN);
        tx_msghdr->data_len = netbuf->len - ETH_HLEN;
        address = (uint64_t)physaddr;
        tx_msghdr->data_buf_addr.high_addr = address >> 32;
        tx_msghdr->data_buf_addr.low_addr = address & 0xffffffff;
        tx_msghdr->metadata_buf_len = 0;
        tx_msghdr->metadata_buf_addr.high_addr = 0;
        tx_msghdr->metadata_buf_addr.low_addr = 0;
        atomic_fetch_add(&commonring->outstanding_tx, 1);
        if (count >= BRCMF_MSGBUF_TX_FLUSH_CNT2) {
            brcmf_commonring_write_complete(commonring);
            count = 0;
        }
    }
    if (count) {
        brcmf_commonring_write_complete(commonring);
    }
    brcmf_commonring_unlock(commonring);
}

static void brcmf_msgbuf_txflow_worker(struct work_struct* worker) {
    struct brcmf_msgbuf* msgbuf;
    uint32_t flowid;

    msgbuf = containerof(worker, struct brcmf_msgbuf, txflow_work);
    for_each_set_bit(flowid, msgbuf->flow_map, msgbuf->max_flowrings) {
        brcmf_clear_bit_in_array(flowid, msgbuf->flow_map);
        brcmf_msgbuf_txflow(msgbuf, flowid);
    }
}

static zx_status_t brcmf_msgbuf_schedule_txdata(struct brcmf_msgbuf* msgbuf, uint32_t flowid,
                                                bool force) {
    struct brcmf_commonring* commonring;

    brcmf_set_bit_in_array(flowid, msgbuf->flow_map);
    commonring = msgbuf->flowrings[flowid];
    if ((force) || (atomic_load(&commonring->outstanding_tx) < BRCMF_MSGBUF_DELAY_TXWORKER_THRS)) {
        workqueue_schedule(msgbuf->txflow_wq, &msgbuf->txflow_work);
    }

    return ZX_OK;
}

static zx_status_t brcmf_msgbuf_tx_queue_data(struct brcmf_pub* drvr, int ifidx,
                                              struct brcmf_netbuf* netbuf) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct brcmf_flowring* flow = msgbuf->flow;
    struct ethhdr* eh = (struct ethhdr*)(netbuf->data);
    uint32_t flowid;
    uint32_t queue_count;
    bool force;

    flowid = brcmf_flowring_lookup(flow, eh->h_dest, netbuf->priority, ifidx);
    if (flowid == BRCMF_FLOWRING_INVALID_ID) {
        flowid = brcmf_msgbuf_flowring_create(msgbuf, ifidx, netbuf);
        if (flowid == BRCMF_FLOWRING_INVALID_ID) {
            return ZX_ERR_NO_MEMORY;
        }
    }
    queue_count = brcmf_flowring_enqueue(flow, flowid, netbuf);
    force = ((queue_count % BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS) == 0);
    brcmf_msgbuf_schedule_txdata(msgbuf, flowid, force);

    return ZX_OK;
}

static void brcmf_msgbuf_configure_addr_mode(struct brcmf_pub* drvr, int ifidx,
                                             enum proto_addr_mode addr_mode) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;

    brcmf_flowring_configure_addr_mode(msgbuf->flow, ifidx, addr_mode);
}

static void brcmf_msgbuf_delete_peer(struct brcmf_pub* drvr, int ifidx, uint8_t peer[ETH_ALEN]) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;

    brcmf_flowring_delete_peer(msgbuf->flow, ifidx, peer);
}

static void brcmf_msgbuf_add_tdls_peer(struct brcmf_pub* drvr, int ifidx, uint8_t peer[ETH_ALEN]) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;

    brcmf_flowring_add_tdls_peer(msgbuf->flow, ifidx, peer);
}

static void brcmf_msgbuf_process_ioctl_complete(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_ioctl_resp_hdr* ioctl_resp;

    ioctl_resp = (struct msgbuf_ioctl_resp_hdr*)buf;

    msgbuf->ioctl_resp_status = ioctl_resp->compl_hdr.status;
    msgbuf->ioctl_resp_ret_len = ioctl_resp->resp_len;
    msgbuf->ioctl_resp_pktid = ioctl_resp->msg.request_id;

    brcmf_msgbuf_ioctl_resp_wake(msgbuf);

    if (msgbuf->cur_ioctlrespbuf) {
        msgbuf->cur_ioctlrespbuf--;
    }
    brcmf_msgbuf_rxbuf_ioctlresp_post(msgbuf);
}

static void brcmf_msgbuf_process_txstatus(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct brcmf_commonring* commonring;
    struct msgbuf_tx_status* tx_status;
    uint32_t idx;
    struct brcmf_netbuf* netbuf;
    uint16_t flowid;

    tx_status = (struct msgbuf_tx_status*)buf;
    idx = tx_status->msg.request_id;
    flowid = tx_status->compl_hdr.flow_ring_id;
    flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
    netbuf = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev, msgbuf->tx_pktids, idx);
    if (!netbuf) {
        return;
    }

    brcmf_set_bit_in_array(flowid, msgbuf->txstatus_done_map);
    commonring = msgbuf->flowrings[flowid];
    atomic_fetch_sub(&commonring->outstanding_tx, 1);

    brcmf_txfinalize(brcmf_get_ifp(msgbuf->drvr, tx_status->msg.ifidx), netbuf, true);
}

static uint32_t brcmf_msgbuf_rxbuf_data_post(struct brcmf_msgbuf* msgbuf, uint32_t count) {
    struct brcmf_commonring* commonring;
    void* ret_ptr;
    struct brcmf_netbuf* netbuf;
    uint16_t alloced;
    uint32_t pktlen;
    dma_addr_t physaddr;
    struct msgbuf_rx_bufpost* rx_bufpost;
    uint64_t address;
    uint32_t pktid;
    uint32_t i;

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_RXPOST_SUBMIT];
    ret_ptr = brcmf_commonring_reserve_for_write_multiple(commonring, count, &alloced);
    if (!ret_ptr) {
        brcmf_dbg(MSGBUF, "Failed to reserve space in commonring\n");
        return 0;
    }

    for (i = 0; i < alloced; i++) {
        rx_bufpost = (struct msgbuf_rx_bufpost*)ret_ptr;
        memset(rx_bufpost, 0, sizeof(*rx_bufpost));

        netbuf = brcmu_pkt_buf_get_netbuf(BRCMF_MSGBUF_MAX_PKT_SIZE);

        if (netbuf == NULL) {
            brcmf_err("Failed to alloc netbuf\n");
            brcmf_commonring_write_cancel(commonring, alloced - i);
            break;
        }

        pktlen = netbuf->len;
        if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids, netbuf, 0,
                                     &physaddr, &pktid)) {
            brcmf_netbuf_free(netbuf);
            brcmf_err("No PKTID available !!\n");
            brcmf_commonring_write_cancel(commonring, alloced - i);
            break;
        }

        if (msgbuf->rx_metadata_offset) {
            address = (uint64_t)physaddr;
            rx_bufpost->metadata_buf_len = msgbuf->rx_metadata_offset;
            rx_bufpost->metadata_buf_addr.high_addr = address >> 32;
            rx_bufpost->metadata_buf_addr.low_addr = address & 0xffffffff;

            brcmf_netbuf_shrink_head(netbuf, msgbuf->rx_metadata_offset);
            pktlen = netbuf->len;
            physaddr += msgbuf->rx_metadata_offset;
        }
        rx_bufpost->msg.msgtype = MSGBUF_TYPE_RXBUF_POST;
        rx_bufpost->msg.request_id = pktid;

        address = (uint64_t)physaddr;
        rx_bufpost->data_buf_len = (uint16_t)pktlen;
        rx_bufpost->data_buf_addr.high_addr = address >> 32;
        rx_bufpost->data_buf_addr.low_addr = address & 0xffffffff;

        ret_ptr += brcmf_commonring_len_item(commonring);
    }

    if (i) {
        brcmf_commonring_write_complete(commonring);
    }

    return i;
}

static void brcmf_msgbuf_rxbuf_data_fill(struct brcmf_msgbuf* msgbuf) {
    uint32_t fillbufs;
    uint32_t retcount;

    fillbufs = msgbuf->max_rxbufpost - msgbuf->rxbufpost;

    while (fillbufs) {
        retcount = brcmf_msgbuf_rxbuf_data_post(msgbuf, fillbufs);
        if (!retcount) {
            break;
        }
        msgbuf->rxbufpost += retcount;
        fillbufs -= retcount;
    }
}

static void brcmf_msgbuf_update_rxbufpost_count(struct brcmf_msgbuf* msgbuf, uint16_t rxcnt) {
    msgbuf->rxbufpost -= rxcnt;
    if (msgbuf->rxbufpost <= (msgbuf->max_rxbufpost - BRCMF_MSGBUF_RXBUFPOST_THRESHOLD)) {
        brcmf_msgbuf_rxbuf_data_fill(msgbuf);
    }
}

static uint32_t brcmf_msgbuf_rxbuf_ctrl_post(struct brcmf_msgbuf* msgbuf, bool event_buf,
                                             uint32_t count) {
    struct brcmf_commonring* commonring;
    void* ret_ptr;
    struct brcmf_netbuf* netbuf;
    uint16_t alloced;
    uint32_t pktlen;
    dma_addr_t physaddr;
    struct msgbuf_rx_ioctl_resp_or_event* rx_bufpost;
    uint64_t address;
    uint32_t pktid;
    uint32_t i;

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
    brcmf_commonring_lock(commonring);
    ret_ptr = brcmf_commonring_reserve_for_write_multiple(commonring, count, &alloced);
    if (!ret_ptr) {
        brcmf_err("Failed to reserve space in commonring\n");
        brcmf_commonring_unlock(commonring);
        return 0;
    }

    for (i = 0; i < alloced; i++) {
        rx_bufpost = (struct msgbuf_rx_ioctl_resp_or_event*)ret_ptr;
        memset(rx_bufpost, 0, sizeof(*rx_bufpost));

        netbuf = brcmu_pkt_buf_get_netbuf(BRCMF_MSGBUF_MAX_PKT_SIZE);

        if (netbuf == NULL) {
            brcmf_err("Failed to alloc netbuf\n");
            brcmf_commonring_write_cancel(commonring, alloced - i);
            break;
        }

        pktlen = netbuf->len;
        if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids, netbuf, 0,
                                     &physaddr, &pktid)) {
            brcmf_netbuf_free(netbuf);
            brcmf_err("No PKTID available !!\n");
            brcmf_commonring_write_cancel(commonring, alloced - i);
            break;
        }
        if (event_buf) {
            rx_bufpost->msg.msgtype = MSGBUF_TYPE_EVENT_BUF_POST;
        } else {
            rx_bufpost->msg.msgtype = MSGBUF_TYPE_IOCTLRESP_BUF_POST;
        }
        rx_bufpost->msg.request_id = pktid;

        address = (uint64_t)physaddr;
        rx_bufpost->host_buf_len = (uint16_t)pktlen;
        rx_bufpost->host_buf_addr.high_addr = address >> 32;
        rx_bufpost->host_buf_addr.low_addr = address & 0xffffffff;

        ret_ptr += brcmf_commonring_len_item(commonring);
    }

    if (i) {
        brcmf_commonring_write_complete(commonring);
    }

    brcmf_commonring_unlock(commonring);

    return i;
}

static void brcmf_msgbuf_rxbuf_ioctlresp_post(struct brcmf_msgbuf* msgbuf) {
    uint32_t count;

    count = msgbuf->max_ioctlrespbuf - msgbuf->cur_ioctlrespbuf;
    count = brcmf_msgbuf_rxbuf_ctrl_post(msgbuf, false, count);
    msgbuf->cur_ioctlrespbuf += count;
}

static void brcmf_msgbuf_rxbuf_event_post(struct brcmf_msgbuf* msgbuf) {
    uint32_t count;

    count = msgbuf->max_eventbuf - msgbuf->cur_eventbuf;
    count = brcmf_msgbuf_rxbuf_ctrl_post(msgbuf, true, count);
    msgbuf->cur_eventbuf += count;
}

static void brcmf_msgbuf_process_event(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_rx_event* event;
    uint32_t idx;
    uint16_t buflen;
    struct brcmf_netbuf* netbuf;
    struct brcmf_if* ifp;

    event = (struct msgbuf_rx_event*)buf;
    idx = event->msg.request_id;
    buflen = event->event_data_len;

    if (msgbuf->cur_eventbuf) {
        msgbuf->cur_eventbuf--;
    }
    brcmf_msgbuf_rxbuf_event_post(msgbuf);

    netbuf = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids, idx);
    if (!netbuf) {
        return;
    }

    if (msgbuf->rx_dataoffset) {
        brcmf_netbuf_shrink_head(netbuf, msgbuf->rx_dataoffset);
    }

    brcmf_netbuf_reduce_length_to(netbuf, buflen);

    ifp = brcmf_get_ifp(msgbuf->drvr, event->msg.ifidx);
    if (!ifp || !ifp->ndev) {
        brcmf_err("Received pkt for invalid ifidx %d\n", event->msg.ifidx);
        goto exit;
    }

    netbuf->protocol = eth_type_trans(netbuf, ifp->ndev);

    brcmf_fweh_process_netbuf(ifp->drvr, netbuf);

exit:
    brcmu_pkt_buf_free_netbuf(netbuf);
}

static void brcmf_msgbuf_process_rx_complete(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_rx_complete* rx_complete;
    struct brcmf_netbuf* netbuf;
    uint16_t data_offset;
    uint16_t buflen;
    uint32_t idx;
    struct brcmf_if* ifp;

    brcmf_msgbuf_update_rxbufpost_count(msgbuf, 1);

    rx_complete = (struct msgbuf_rx_complete*)buf;
    data_offset = rx_complete->data_offset;
    buflen = rx_complete->data_len;
    idx = rx_complete->msg.request_id;

    netbuf = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev, msgbuf->rx_pktids, idx);
    if (!netbuf) {
        return;
    }

    if (data_offset) {
        brcmf_netbuf_shrink_head(netbuf, data_offset);
    } else if (msgbuf->rx_dataoffset) {
        brcmf_netbuf_shrink_head(netbuf, msgbuf->rx_dataoffset);
    }

    brcmf_netbuf_reduce_length_to(netbuf, buflen);

    ifp = brcmf_get_ifp(msgbuf->drvr, rx_complete->msg.ifidx);
    if (!ifp || !ifp->ndev) {
        brcmf_err("Received pkt for invalid ifidx %d\n", rx_complete->msg.ifidx);
        brcmu_pkt_buf_free_netbuf(netbuf);
        return;
    }

    netbuf->protocol = eth_type_trans(netbuf, ifp->ndev);
    brcmf_netif_rx(ifp, netbuf);
}

static void brcmf_msgbuf_process_flow_ring_create_response(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_flowring_create_resp* flowring_create_resp;
    uint16_t status;
    uint16_t flowid;

    flowring_create_resp = (struct msgbuf_flowring_create_resp*)buf;

    flowid = flowring_create_resp->compl_hdr.flow_ring_id;
    flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
    status = flowring_create_resp->compl_hdr.status;

    if (status) {
        brcmf_err("Flowring creation failed, code %d\n", status);
        brcmf_msgbuf_remove_flowring(msgbuf, flowid);
        return;
    }
    brcmf_dbg(MSGBUF, "Flowring %d Create response status %d\n", flowid, status);

    brcmf_flowring_open(msgbuf->flow, flowid);

    brcmf_msgbuf_schedule_txdata(msgbuf, flowid, true);
}

static void brcmf_msgbuf_process_flow_ring_delete_response(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_flowring_delete_resp* flowring_delete_resp;
    uint16_t status;
    uint16_t flowid;

    flowring_delete_resp = (struct msgbuf_flowring_delete_resp*)buf;

    flowid = flowring_delete_resp->compl_hdr.flow_ring_id;
    flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
    status = flowring_delete_resp->compl_hdr.status;

    if (status) {
        brcmf_err("Flowring deletion failed, code %d\n", status);
        brcmf_flowring_delete(msgbuf->flow, flowid);
        return;
    }
    brcmf_dbg(MSGBUF, "Flowring %d Delete response status %d\n", flowid, status);

    brcmf_msgbuf_remove_flowring(msgbuf, flowid);
}

static void brcmf_msgbuf_process_msgtype(struct brcmf_msgbuf* msgbuf, void* buf) {
    struct msgbuf_common_hdr* msg;

    msg = (struct msgbuf_common_hdr*)buf;
    switch (msg->msgtype) {
    case MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT\n");
        brcmf_msgbuf_process_flow_ring_create_response(msgbuf, buf);
        break;
    case MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT\n");
        brcmf_msgbuf_process_flow_ring_delete_response(msgbuf, buf);
        break;
    case MSGBUF_TYPE_IOCTLPTR_REQ_ACK:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_IOCTLPTR_REQ_ACK\n");
        break;
    case MSGBUF_TYPE_IOCTL_CMPLT:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_IOCTL_CMPLT\n");
        brcmf_msgbuf_process_ioctl_complete(msgbuf, buf);
        break;
    case MSGBUF_TYPE_WL_EVENT:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_WL_EVENT\n");
        brcmf_msgbuf_process_event(msgbuf, buf);
        break;
    case MSGBUF_TYPE_TX_STATUS:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_TX_STATUS\n");
        brcmf_msgbuf_process_txstatus(msgbuf, buf);
        break;
    case MSGBUF_TYPE_RX_CMPLT:
        brcmf_dbg(MSGBUF, "MSGBUF_TYPE_RX_CMPLT\n");
        brcmf_msgbuf_process_rx_complete(msgbuf, buf);
        break;
    default:
        brcmf_err("Unsupported msgtype %d\n", msg->msgtype);
        break;
    }
}

static void brcmf_msgbuf_process_rx(struct brcmf_msgbuf* msgbuf,
                                    struct brcmf_commonring* commonring) {
    void* buf;
    uint16_t count;
    uint16_t processed;

again:
    buf = brcmf_commonring_get_read_ptr(commonring, &count);
    if (buf == NULL) {
        return;
    }

    processed = 0;
    while (count) {
        brcmf_msgbuf_process_msgtype(msgbuf, buf + msgbuf->rx_dataoffset);
        buf += brcmf_commonring_len_item(commonring);
        processed++;
        if (processed == BRCMF_MSGBUF_UPDATE_RX_PTR_THRS) {
            brcmf_commonring_read_complete(commonring, processed);
            processed = 0;
        }
        count--;
    }
    if (processed) {
        brcmf_commonring_read_complete(commonring, processed);
    }

    if (commonring->r_ptr == 0) {
        goto again;
    }
}

zx_status_t brcmf_proto_msgbuf_rx_trigger(struct brcmf_device* dev) {
    struct brcmf_bus* bus_if = dev_to_bus(dev);
    struct brcmf_pub* drvr = bus_if->drvr;
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct brcmf_commonring* commonring;
    void* buf;
    uint32_t flowid;
    int qlen;

    buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_RX_COMPLETE];
    brcmf_msgbuf_process_rx(msgbuf, buf);
    buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_TX_COMPLETE];
    brcmf_msgbuf_process_rx(msgbuf, buf);
    buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_CONTROL_COMPLETE];
    brcmf_msgbuf_process_rx(msgbuf, buf);

    for_each_set_bit(flowid, msgbuf->txstatus_done_map, msgbuf->max_flowrings) {
        brcmf_clear_bit_in_array(flowid, msgbuf->txstatus_done_map);
        commonring = msgbuf->flowrings[flowid];
        qlen = brcmf_flowring_qlen(msgbuf->flow, flowid);
        if ((qlen > BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS) ||
                ((qlen) &&
                 (atomic_load(&commonring->outstanding_tx) < BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS))) {
            brcmf_msgbuf_schedule_txdata(msgbuf, flowid, true);
        }
    }

    return ZX_OK;
}

void brcmf_msgbuf_delete_flowring(struct brcmf_pub* drvr, uint16_t flowid) {
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct msgbuf_tx_flowring_delete_req* delete;
    struct brcmf_commonring* commonring;
    void* ret_ptr;
    uint8_t ifidx;
    zx_status_t err;

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
    brcmf_commonring_lock(commonring);
    ret_ptr = brcmf_commonring_reserve_for_write(commonring);
    if (!ret_ptr) {
        brcmf_err("FW unaware, flowring will be removed !!\n");
        brcmf_commonring_unlock(commonring);
        brcmf_msgbuf_remove_flowring(msgbuf, flowid);
        return;
    }

    delete = (struct msgbuf_tx_flowring_delete_req*)ret_ptr;

    ifidx = brcmf_flowring_ifidx_get(msgbuf->flow, flowid);

    delete->msg.msgtype = MSGBUF_TYPE_FLOW_RING_DELETE;
    delete->msg.ifidx = ifidx;
    delete->msg.request_id = 0;

    delete->flow_ring_id = flowid + BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
    delete->reason = 0;

    brcmf_dbg(MSGBUF, "Send Flow Delete Req flow ID %d, ifindex %d\n", flowid, ifidx);

    err = brcmf_commonring_write_complete(commonring);
    brcmf_commonring_unlock(commonring);
    if (err != ZX_OK) {
        brcmf_err("Failed to submit RING_DELETE, flowring will be removed\n");
        brcmf_msgbuf_remove_flowring(msgbuf, flowid);
    }
}

#ifdef DEBUG
static zx_status_t brcmf_msgbuf_stats_read(struct seq_file* seq, void* data) {
    struct brcmf_bus* bus_if = dev_to_bus(seq->private);
    struct brcmf_pub* drvr = bus_if->drvr;
    struct brcmf_msgbuf* msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
    struct brcmf_commonring* commonring;
    uint16_t i;
    struct brcmf_flowring_ring* ring;
    struct brcmf_flowring_hash* hash;

    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
    seq_printf(seq, "h2d_ctl_submit: rp %4u, wp %4u, depth %4u\n", commonring->r_ptr,
               commonring->w_ptr, commonring->depth);
    commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_RXPOST_SUBMIT];
    seq_printf(seq, "h2d_rx_submit:  rp %4u, wp %4u, depth %4u\n", commonring->r_ptr,
               commonring->w_ptr, commonring->depth);
    commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_CONTROL_COMPLETE];
    seq_printf(seq, "d2h_ctl_cmplt:  rp %4u, wp %4u, depth %4u\n", commonring->r_ptr,
               commonring->w_ptr, commonring->depth);
    commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_TX_COMPLETE];
    seq_printf(seq, "d2h_tx_cmplt:   rp %4u, wp %4u, depth %4u\n", commonring->r_ptr,
               commonring->w_ptr, commonring->depth);
    commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_RX_COMPLETE];
    seq_printf(seq, "d2h_rx_cmplt:   rp %4u, wp %4u, depth %4u\n", commonring->r_ptr,
               commonring->w_ptr, commonring->depth);

    seq_printf(seq, "\nh2d_flowrings: depth %u\n", BRCMF_H2D_TXFLOWRING_MAX_ITEM);
    seq_puts(seq, "Active flowrings:\n");
    hash = msgbuf->flow->hash;
    for (i = 0; i < msgbuf->flow->nrofrings; i++) {
        if (!msgbuf->flow->rings[i]) {
            continue;
        }
        ring = msgbuf->flow->rings[i];
        if (ring->status != RING_OPEN) {
            continue;
        }
        commonring = msgbuf->flowrings[i];
        hash = &msgbuf->flow->hash[ring->hash_id];
        seq_printf(seq,
                   "id %3u: rp %4u, wp %4u, qlen %4u, blocked %u\n"
                   "        ifidx %u, fifo %u, da %pM\n",
                   i, commonring->r_ptr, commonring->w_ptr,
                   brcmf_netbuf_list_length(&ring->netbuf_list), ring->blocked, hash->ifidx,
                   hash->fifo, hash->mac);
    }

    return ZX_OK;
}
#else
static zx_status_t brcmf_msgbuf_stats_read(struct seq_file* seq, void* data) {
    return ZX_OK;
}
#endif

zx_status_t brcmf_proto_msgbuf_attach(struct brcmf_pub* drvr) {
    struct brcmf_bus_msgbuf* if_msgbuf;
    struct brcmf_msgbuf* msgbuf;
    uint64_t address;
    uint32_t count;

    if_msgbuf = drvr->bus_if->msgbuf;

    brcmf_dbg(TEMP, "ENTER");
    if (if_msgbuf->max_flowrings >= BRCMF_FLOWRING_HASHSIZE) {
        brcmf_err("driver not configured for this many flowrings %d\n", if_msgbuf->max_flowrings);
        if_msgbuf->max_flowrings = BRCMF_FLOWRING_HASHSIZE - 1;
    }

    msgbuf = calloc(1, sizeof(*msgbuf));
    if (!msgbuf) {
        goto fail;
    }

    msgbuf->txflow_wq = workqueue_create("msgbuf_txflow");
    if (msgbuf->txflow_wq == NULL) {
        brcmf_err("workqueue creation failed\n");
        goto fail;
    }
    workqueue_init_work(&msgbuf->txflow_work, brcmf_msgbuf_txflow_worker);
    count = BITS_TO_LONGS(if_msgbuf->max_flowrings);
    count = count * sizeof(unsigned long);
    msgbuf->flow_map = calloc(1, count);
    if (!msgbuf->flow_map) {
        goto fail;
    }

    msgbuf->txstatus_done_map = calloc(1, count);
    if (!msgbuf->txstatus_done_map) {
        goto fail;
    }

    msgbuf->drvr = drvr;
    msgbuf->ioctbuf = dma_alloc_coherent(drvr->bus_if->dev, BRCMF_TX_IOCTL_MAX_MSG_SIZE,
                                         &msgbuf->ioctbuf_handle, GFP_KERNEL);
    if (!msgbuf->ioctbuf) {
        goto fail;
    }
    address = (uint64_t)msgbuf->ioctbuf_handle;
    msgbuf->ioctbuf_phys_hi = address >> 32;
    msgbuf->ioctbuf_phys_lo = address & 0xffffffff;

    drvr->proto->hdrpull = brcmf_msgbuf_hdrpull;
    drvr->proto->query_dcmd = brcmf_msgbuf_query_dcmd;
    drvr->proto->set_dcmd = brcmf_msgbuf_set_dcmd;
    drvr->proto->tx_queue_data = brcmf_msgbuf_tx_queue_data;
    drvr->proto->configure_addr_mode = brcmf_msgbuf_configure_addr_mode;
    drvr->proto->delete_peer = brcmf_msgbuf_delete_peer;
    drvr->proto->add_tdls_peer = brcmf_msgbuf_add_tdls_peer;
    drvr->proto->rxreorder = brcmf_msgbuf_rxreorder;
    drvr->proto->pd = msgbuf;

    msgbuf->ioctl_resp_wait = COMPLETION_INIT;

    msgbuf->commonrings = (struct brcmf_commonring**)if_msgbuf->commonrings;
    msgbuf->flowrings = (struct brcmf_commonring**)if_msgbuf->flowrings;
    msgbuf->max_flowrings = if_msgbuf->max_flowrings;
    msgbuf->flowring_dma_handle =
        calloc(1, msgbuf->max_flowrings * sizeof(*msgbuf->flowring_dma_handle));
    if (!msgbuf->flowring_dma_handle) {
        goto fail;
    }

    msgbuf->rx_dataoffset = if_msgbuf->rx_dataoffset;
    msgbuf->max_rxbufpost = if_msgbuf->max_rxbufpost;

    msgbuf->max_ioctlrespbuf = BRCMF_MSGBUF_MAX_IOCTLRESPBUF_POST;
    msgbuf->max_eventbuf = BRCMF_MSGBUF_MAX_EVENTBUF_POST;

    msgbuf->tx_pktids = brcmf_msgbuf_init_pktids(NR_TX_PKTIDS, DMA_TO_DEVICE);
    if (!msgbuf->tx_pktids) {
        goto fail;
    }
    msgbuf->rx_pktids = brcmf_msgbuf_init_pktids(NR_RX_PKTIDS, DMA_FROM_DEVICE);
    if (!msgbuf->rx_pktids) {
        goto fail;
    }

    msgbuf->flow = brcmf_flowring_attach(drvr->bus_if->dev, if_msgbuf->max_flowrings);
    if (!msgbuf->flow) {
        goto fail;
    }

    brcmf_dbg(MSGBUF, "Feeding buffers, rx data %d, rx event %d, rx ioctl resp %d\n",
              msgbuf->max_rxbufpost, msgbuf->max_eventbuf, msgbuf->max_ioctlrespbuf);
    count = 0;
    do {
        brcmf_msgbuf_rxbuf_data_fill(msgbuf);
        if (msgbuf->max_rxbufpost != msgbuf->rxbufpost) {
            msleep(10);
        } else {
            break;
        }
        count++;
    } while (count < 10);
    brcmf_msgbuf_rxbuf_event_post(msgbuf);
    brcmf_msgbuf_rxbuf_ioctlresp_post(msgbuf);

    workqueue_init_work(&msgbuf->flowring_work, brcmf_msgbuf_flowring_worker);
    //spin_lock_init(&msgbuf->flowring_work_lock);
    list_initialize(&msgbuf->work_queue);

    brcmf_debugfs_add_entry(drvr, "msgbuf_stats", brcmf_msgbuf_stats_read);

    return ZX_OK;

fail:
    if (msgbuf) {
        free(msgbuf->flow_map);
        free(msgbuf->txstatus_done_map);
        brcmf_msgbuf_release_pktids(msgbuf);
        free(msgbuf->flowring_dma_handle);
        if (msgbuf->ioctbuf)
            dma_free_coherent(drvr->bus_if->dev, BRCMF_TX_IOCTL_MAX_MSG_SIZE, msgbuf->ioctbuf,
                              msgbuf->ioctbuf_handle);
        free(msgbuf);
    }
    return ZX_ERR_NO_MEMORY;
}

void brcmf_proto_msgbuf_detach(struct brcmf_pub* drvr) {
    struct brcmf_msgbuf* msgbuf;
    struct brcmf_msgbuf_work_item* work;

    brcmf_dbg(TRACE, "Enter\n");
    if (drvr->proto->pd) {
        msgbuf = (struct brcmf_msgbuf*)drvr->proto->pd;
        workqueue_cancel_work(&msgbuf->flowring_work);
        while (!list_is_empty(&msgbuf->work_queue)) {
            work = list_peek_head_type(&msgbuf->work_queue, struct brcmf_msgbuf_work_item, queue);
            list_delete(&work->queue);
            free(work);
        }
        free(msgbuf->flow_map);
        free(msgbuf->txstatus_done_map);
        if (msgbuf->txflow_wq) {
            workqueue_destroy(msgbuf->txflow_wq);
        }

        brcmf_flowring_detach(msgbuf->flow);
        dma_free_coherent(drvr->bus_if->dev, BRCMF_TX_IOCTL_MAX_MSG_SIZE, msgbuf->ioctbuf,
                          msgbuf->ioctbuf_handle);
        brcmf_msgbuf_release_pktids(msgbuf);
        free(msgbuf->flowring_dma_handle);
        free(msgbuf);
        drvr->proto->pd = NULL;
    }
}
