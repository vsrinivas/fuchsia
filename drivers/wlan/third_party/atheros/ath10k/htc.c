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

#include <string.h>

#include "core.h"
#include "hif.h"
#include "debug.h"

/********/
/* Send */
/********/

static void ath10k_htc_control_tx_complete(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    ath10k_msg_buf_free(msg_buf);
}

void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep* ep, struct ath10k_msg_buf* msg_buf) {
    if (!ep->ep_ops.ep_tx_complete) {
        ath10k_warn("no tx handler for eid %d\n", ep->eid);
        ath10k_msg_buf_free(msg_buf);
        return;
    }

    ep->ep_ops.ep_tx_complete(ep->htc->ar, msg_buf);
}

static void ath10k_htc_prepare_tx_buf(struct ath10k_htc_ep* ep,
                                      struct ath10k_msg_buf* msg_buf) {
    struct ath10k_htc_hdr* hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC);
    hdr->eid = ep->eid;
    hdr->len = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_HTC);
    hdr->flags = ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE;

    mtx_lock(&ep->htc->tx_lock);
    hdr->seq_no = ep->seq_no++;
    mtx_unlock(&ep->htc->tx_lock);
}

zx_status_t ath10k_htc_send(struct ath10k_htc* htc,
                            enum ath10k_htc_ep_id eid,
                            struct ath10k_msg_buf* msg_buf) {
    struct ath10k* ar = htc->ar;
    struct ath10k_htc_ep* ep = &htc->endpoint[eid];
    struct ath10k_hif_sg_item sg_item;
    int credits = 0;
    int ret;

    if (htc->ar->state == ATH10K_STATE_WEDGED) {
        return ZX_ERR_BAD_STATE;
    }

    if (eid >= ATH10K_HTC_EP_COUNT) {
        ath10k_warn("Invalid endpoint id: %d\n", eid);
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (ep->tx_credit_flow_enabled) {
        credits = DIV_ROUNDUP(msg_buf->used, htc->target_credit_size);
        mtx_lock(&htc->tx_lock);
        if (ep->tx_credits < credits) {
            ath10k_dbg(ar, ATH10K_DBG_HTC,
                       "htc insufficient credits ep %d required %d available %d\n",
                       eid, credits, ep->tx_credits);
            mtx_unlock(&htc->tx_lock);
            ret = ZX_ERR_SHOULD_WAIT;
            goto err_done;
        }
        ep->tx_credits -= credits;
        ath10k_dbg(ar, ATH10K_DBG_HTC,
                   "htc ep %d consumed %d credits (total %d)\n",
                   eid, credits, ep->tx_credits);
        mtx_unlock(&htc->tx_lock);
    }

    ath10k_htc_prepare_tx_buf(ep, msg_buf);

    sg_item.transfer_id = ep->eid;
    sg_item.transfer_context = msg_buf;
    sg_item.vaddr = msg_buf->vaddr;
    sg_item.paddr = msg_buf->paddr;
    sg_item.len = msg_buf->used;

    ret = ath10k_hif_tx_sg(htc->ar, ep->ul_pipe_id, &sg_item, 1);
    if (ret != ZX_OK) {
        goto err_credits;
    }

    return ZX_OK;

err_credits:
    if (ep->tx_credit_flow_enabled) {
        mtx_lock(&htc->tx_lock);
        ep->tx_credits += credits;
        ath10k_dbg(ar, ATH10K_DBG_HTC,
                   "htc ep %d reverted %d credits back (total %d)\n",
                   eid, credits, ep->tx_credits);
        mtx_unlock(&htc->tx_lock);

        if (ep->ep_ops.ep_tx_credits) {
            ep->ep_ops.ep_tx_credits(htc->ar);
        }
    }
err_done:
    return ret;
}

void ath10k_htc_tx_completion_handler(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct ath10k_htc* htc = &ar->htc;
    struct ath10k_htc_ep* ep;

    struct ath10k_htc_hdr* htc_hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC);
    ep = &htc->endpoint[htc_hdr->eid];

    ath10k_htc_notify_tx_completion(ep, msg_buf);
    /* the msg_buf now belongs to the completion handler */
}

/***********/
/* Receive */
/***********/

static void
ath10k_htc_process_credit_report(struct ath10k_htc* htc,
                                 const struct ath10k_htc_credit_report* report,
                                 int len,
                                 enum ath10k_htc_ep_id eid) {
    struct ath10k* ar = htc->ar;
    struct ath10k_htc_ep* ep;
    int i, n_reports;

    if (len % sizeof(*report)) {
        ath10k_warn("Uneven credit report len %d", len);
    }

    n_reports = len / sizeof(*report);

    mtx_lock(&htc->tx_lock);
    for (i = 0; i < n_reports; i++, report++) {
        if (report->eid >= ATH10K_HTC_EP_COUNT) {
            break;
        }

        ep = &htc->endpoint[report->eid];
        ep->tx_credits += report->credits;

        ath10k_dbg(ar, ATH10K_DBG_HTC, "htc ep %d got %d credits (total %d)\n",
                   report->eid, report->credits, ep->tx_credits);

        if (ep->ep_ops.ep_tx_credits) {
            mtx_unlock(&htc->tx_lock);
            ep->ep_ops.ep_tx_credits(htc->ar);
            mtx_lock(&htc->tx_lock);
        }
    }
    mtx_unlock(&htc->tx_lock);
}

static zx_status_t
ath10k_htc_process_lookahead(struct ath10k_htc* htc,
                             const struct ath10k_htc_lookahead_report* report,
                             int len,
                             enum ath10k_htc_ep_id eid,
                             void* next_lookaheads,
                             int* next_lookaheads_len) {
    struct ath10k* ar = htc->ar;

    /* Invalid lookahead flags are actually transmitted by
     * the target in the HTC control message.
     * Since this will happen at every boot we silently ignore
     * the lookahead in this case
     */
    if (report->pre_valid != ((~report->post_valid) & 0xFF)) {
        return ZX_OK;
    }

    if (next_lookaheads && next_lookaheads_len) {
        ath10k_dbg(ar, ATH10K_DBG_HTC,
                   "htc rx lookahead found pre_valid 0x%x post_valid 0x%x\n",
                   report->pre_valid, report->post_valid);

        /* look ahead bytes are valid, copy them over */
        memcpy((uint8_t*)next_lookaheads, report->lookahead, 4);

        *next_lookaheads_len = 1;
    }

    return ZX_OK;
}

static zx_status_t
ath10k_htc_process_lookahead_bundle(struct ath10k_htc* htc,
                                    const struct ath10k_htc_lookahead_bundle* report,
                                    int len,
                                    enum ath10k_htc_ep_id eid,
                                    void* next_lookaheads,
                                    int* next_lookaheads_len) {
    int bundle_cnt = len / sizeof(*report);

    if (!bundle_cnt || (bundle_cnt > HTC_HOST_MAX_MSG_PER_BUNDLE)) {
        ath10k_warn("Invalid lookahead bundle count: %d\n",
                    bundle_cnt);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    if (next_lookaheads && next_lookaheads_len) {
        int i;

        for (i = 0; i < bundle_cnt; i++) {
            memcpy(((uint8_t*)next_lookaheads) + 4 * i,
                   report->lookahead, 4);
            report++;
        }

        *next_lookaheads_len = bundle_cnt;
    }

    return ZX_OK;
}

zx_status_t ath10k_htc_process_trailer(struct ath10k_htc* htc,
                                       uint8_t* buffer,
                                       int length,
                                       enum ath10k_htc_ep_id src_eid,
                                       void* next_lookaheads,
                                       int* next_lookaheads_len) {
    struct ath10k_htc_lookahead_bundle* bundle;
    struct ath10k* ar = htc->ar;
    int status = ZX_OK;
    struct ath10k_htc_record* record;
    uint8_t* orig_buffer;
    int orig_length;
    size_t len;

    orig_buffer = buffer;
    orig_length = length;

    while (length > 0) {
        record = (struct ath10k_htc_record*)buffer;

        if (length < (int)sizeof(record->hdr)) {
            status = ZX_ERR_BUFFER_TOO_SMALL;
            break;
        }

        if (record->hdr.len > length) {
            /* no room left in buffer for record */
            ath10k_warn("Invalid record length: %d\n",
                        record->hdr.len);
            status = ZX_ERR_BUFFER_TOO_SMALL;
            break;
        }

        switch (record->hdr.id) {
        case ATH10K_HTC_RECORD_CREDITS:
            len = sizeof(struct ath10k_htc_credit_report);
            if (record->hdr.len < len) {
                ath10k_warn("Credit report too long\n");
                status = ZX_ERR_BUFFER_TOO_SMALL;
                break;
            }
            ath10k_htc_process_credit_report(htc,
                                             record->credit_report,
                                             record->hdr.len,
                                             src_eid);
            break;
        case ATH10K_HTC_RECORD_LOOKAHEAD:
            len = sizeof(struct ath10k_htc_lookahead_report);
            if (record->hdr.len < len) {
                ath10k_warn("Lookahead report too long\n");
                status = ZX_ERR_BUFFER_TOO_SMALL;
                break;
            }
            status = ath10k_htc_process_lookahead(htc,
                                                  record->lookahead_report,
                                                  record->hdr.len,
                                                  src_eid,
                                                  next_lookaheads,
                                                  next_lookaheads_len);
            break;
        case ATH10K_HTC_RECORD_LOOKAHEAD_BUNDLE:
            bundle = record->lookahead_bundle;
            status = ath10k_htc_process_lookahead_bundle(htc,
                     bundle,
                     record->hdr.len,
                     src_eid,
                     next_lookaheads,
                     next_lookaheads_len);
            break;
        default:
            ath10k_warn("Unhandled record: id:%d length:%d\n",
                        record->hdr.id, record->hdr.len);
            break;
        }

        if (status != ZX_OK) {
            break;
        }

        /* multiple records may be present in a trailer */
        buffer += sizeof(record->hdr) + record->hdr.len;
        length -= sizeof(record->hdr) + record->hdr.len;
    }

    if (status != ZX_OK)
        ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc rx bad trailer", "",
                        orig_buffer, orig_length);

    return status;
}

void ath10k_htc_rx_completion_handler(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    int status = ZX_OK;
    struct ath10k_htc* htc = &ar->htc;
    struct ath10k_htc_hdr* hdr;
    struct ath10k_htc_ep* ep;
    uint16_t payload_len;
    uint32_t trailer_len = 0;
    size_t min_len;
    uint8_t eid;
    bool trailer_present;

    hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC);

    msg_buf->used = sizeof(*hdr) + hdr->len;
    ZX_DEBUG_ASSERT(msg_buf->used <= msg_buf->capacity);

    eid = hdr->eid;

    if (eid >= ATH10K_HTC_EP_COUNT) {
        ath10k_warn("HTC Rx: invalid eid %d\n", eid);
        ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad header", "",
                        hdr, sizeof(*hdr));
        goto out;
    }

    ep = &htc->endpoint[eid];

    payload_len = hdr->len;

    if (payload_len + sizeof(*hdr) > ATH10K_HTC_MAX_LEN) {
        ath10k_warn("HTC rx frame too long, len: %zu\n",
                    payload_len + sizeof(*hdr));
        ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad rx pkt len", "",
                        hdr, sizeof(*hdr));
        goto out;
    }

    size_t actual_payload_sz = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_HTC);
    if (actual_payload_sz < payload_len) {
        ath10k_err("HTC Rx: insufficient length, got %zu, expected %d\n",
                   actual_payload_sz, payload_len);
        ath10k_dbg_dump(ar, ATH10K_DBG_HTC, "htc bad rx pkt len",
                        "", hdr, sizeof(*hdr));
        goto out;
    }

    /* get flags to check for trailer */
    trailer_present = hdr->flags & ATH10K_HTC_FLAG_TRAILER_PRESENT;
    if (trailer_present) {
        uint8_t* trailer;

        trailer_len = hdr->trailer_len;
        min_len = sizeof(struct ath10k_ath10k_htc_record_hdr);

        if ((trailer_len < min_len) ||
                (trailer_len > payload_len)) {
            ath10k_warn("Invalid trailer length: %d\n",
                        trailer_len);
            goto out;
        }

        trailer = (uint8_t*)hdr;
        trailer += sizeof(*hdr);
        trailer += payload_len;
        trailer -= trailer_len;
        status = ath10k_htc_process_trailer(htc, trailer,
                                            trailer_len, hdr->eid,
                                            NULL, NULL);
        if (status) {
            goto out;
        }

        msg_buf->used -= trailer_len;
    }

    if (((int)payload_len - (int)trailer_len) <= 0)
        /* zero length packet with trailer data, just drop these */
    {
        goto out;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTC, "htc rx completion ep %d msg_buf %pK\n",
               eid, msg_buf);
    ep->ep_ops.ep_rx_complete(ar, msg_buf);

    /* msg_buf is now owned by the rx completion handler */
    return;

out:
    ath10k_msg_buf_free(msg_buf);
}

static void ath10k_htc_control_rx_complete(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct ath10k_htc* htc = &ar->htc;
    struct ath10k_htc_msg* msg = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC_MSG);

    switch (msg->hdr.message_id) {
    case ATH10K_HTC_MSG_READY_ID:
    case ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID:
        /* handle HTC control message */
        if (completion_wait(&htc->ctl_resp, 0) == ZX_OK) {
            /* this is a fatal error, target should not be
             * sending unsolicited messages on the ep 0
             */
            ath10k_warn("HTC rx ctrl still processing\n");
            completion_signal(&htc->ctl_resp);
            goto out;
        }

        size_t msg_len = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_HTC_MSG);
        htc->control_resp_len =
            MIN_T(int, msg_len, ATH10K_HTC_MAX_CTRL_MSG_LEN);

        memcpy(htc->control_resp_buffer, msg, htc->control_resp_len);

        completion_signal(&htc->ctl_resp);
        break;
    case ATH10K_HTC_MSG_SEND_SUSPEND_COMPLETE:
        htc->htc_ops.target_send_suspend_complete(ar);
        break;
    default:
        ath10k_warn("ignoring unsolicited htc ep0 event\n");
        break;
    }

out:
    ath10k_msg_buf_free(msg_buf);
}

/***************/
/* Init/Deinit */
/***************/

static const char* htc_service_name(enum ath10k_htc_svc_id id) {
    switch (id) {
    case ATH10K_HTC_SVC_ID_RESERVED:
        return "Reserved";
    case ATH10K_HTC_SVC_ID_RSVD_CTRL:
        return "Control";
    case ATH10K_HTC_SVC_ID_WMI_CONTROL:
        return "WMI";
    case ATH10K_HTC_SVC_ID_WMI_DATA_BE:
        return "DATA BE";
    case ATH10K_HTC_SVC_ID_WMI_DATA_BK:
        return "DATA BK";
    case ATH10K_HTC_SVC_ID_WMI_DATA_VI:
        return "DATA VI";
    case ATH10K_HTC_SVC_ID_WMI_DATA_VO:
        return "DATA VO";
    case ATH10K_HTC_SVC_ID_NMI_CONTROL:
        return "NMI Control";
    case ATH10K_HTC_SVC_ID_NMI_DATA:
        return "NMI Data";
    case ATH10K_HTC_SVC_ID_HTT_DATA_MSG:
        return "HTT Data";
    case ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS:
        return "RAW";
    }

    return "Unknown";
}

static void ath10k_htc_reset_endpoint_states(struct ath10k_htc* htc) {
    struct ath10k_htc_ep* ep;
    int i;

    for (i = ATH10K_HTC_EP_0; i < ATH10K_HTC_EP_COUNT; i++) {
        ep = &htc->endpoint[i];
        ep->service_id = ATH10K_HTC_SVC_ID_UNUSED;
        ep->max_ep_message_len = 0;
        ep->max_tx_queue_depth = 0;
        ep->eid = i;
        ep->htc = htc;
        ep->tx_credit_flow_enabled = true;
    }
}

static uint8_t ath10k_htc_get_credit_allocation(struct ath10k_htc* htc,
                                                uint16_t service_id) {
    uint8_t allocation = 0;

    /* The WMI control service is the only service with flow control.
     * Let it have all transmit credits.
     */
    if (service_id == ATH10K_HTC_SVC_ID_WMI_CONTROL) {
        allocation = htc->total_transmit_credits;
    }

    return allocation;
}

static zx_status_t ath10k_htc_wait_ctl_resp(struct ath10k_htc* htc) {
    struct ath10k* ar = htc->ar;
    int i;
    zx_status_t status = completion_wait(&htc->ctl_resp, ATH10K_HTC_WAIT_TIMEOUT);
    if (status == ZX_ERR_TIMED_OUT) {
        /* Workaround: In some cases the PCI HIF doesn't
         * receive interrupt for the control response message
         * even if the buffer was completed. It is suspected
         * iomap writes unmasking PCI CE irqs aren't propagated
         * properly in KVM PCI-passthrough sometimes.
         * Some symptoms are described in NET-992.
         */
        ath10k_warn("failed to receive control response completion, polling..\n");

        for (i = 0; i < CE_COUNT; i++) {
            ath10k_hif_send_complete_check(ar, i, 1);
        }

        status = completion_wait(&htc->ctl_resp, ATH10K_HTC_WAIT_TIMEOUT);
    }
    return status;
}

zx_status_t ath10k_htc_wait_target(struct ath10k_htc* htc) {
    struct ath10k* ar = htc->ar;
    zx_status_t status = ZX_OK;
    struct ath10k_htc_msg* msg;
    uint16_t message_id;

    status = ath10k_htc_wait_ctl_resp(htc);
    if (status != ZX_OK) {
        ath10k_err("ctl_resp never came in (%d)\n", status);
        return status;
    }

    if ((size_t)htc->control_resp_len < sizeof(msg->hdr) + sizeof(msg->ready)) {
        ath10k_err("Invalid HTC ready msg len:%d\n",
                   htc->control_resp_len);
        return ZX_ERR_IO;
    }

    msg = (struct ath10k_htc_msg*)htc->control_resp_buffer;
    message_id   = msg->hdr.message_id;

    if (message_id != ATH10K_HTC_MSG_READY_ID) {
        ath10k_err("Invalid HTC ready msg: 0x%x\n", message_id);
        return ZX_ERR_IO;
    }

    htc->total_transmit_credits = msg->ready.credit_count;
    htc->target_credit_size = msg->ready.credit_size;

    ath10k_dbg(ar, ATH10K_DBG_HTC,
               "Target ready! transmit resources: %d size:%d\n",
               htc->total_transmit_credits,
               htc->target_credit_size);

    if ((htc->total_transmit_credits == 0) ||
            (htc->target_credit_size == 0)) {
        ath10k_err("Invalid credit size received\n");
        return ZX_ERR_IO;
    }

    /* The only way to determine if the ready message is an extended
     * message is from the size.
     */
    if ((size_t)htc->control_resp_len >=
            sizeof(msg->hdr) + sizeof(msg->ready_ext)) {
        htc->max_msgs_per_htc_bundle =
            MIN_T(uint8_t, msg->ready_ext.max_msgs_per_htc_bundle,
                  HTC_HOST_MAX_MSG_PER_BUNDLE);
        ath10k_dbg(ar, ATH10K_DBG_HTC,
                   "Extended ready message. RX bundle size: %d\n",
                   htc->max_msgs_per_htc_bundle);
    }

    return ZX_OK;
}

zx_status_t ath10k_htc_connect_service(struct ath10k_htc* htc,
                                       struct ath10k_htc_svc_conn_req* conn_req,
                                       struct ath10k_htc_svc_conn_resp* conn_resp) {
    struct ath10k* ar = htc->ar;
    struct ath10k_htc_msg* msg;
    struct ath10k_htc_conn_svc* req_msg;
    struct ath10k_htc_conn_svc_response resp_msg_dummy;
    struct ath10k_htc_conn_svc_response* resp_msg = &resp_msg_dummy;
    enum ath10k_htc_ep_id assigned_eid = ATH10K_HTC_EP_COUNT;
    struct ath10k_htc_ep* ep;
    struct ath10k_msg_buf* msg_buf;
    unsigned int max_msg_size = 0;
    int resp_length;
    zx_status_t status;
    bool disable_credit_flow_ctrl = false;
    uint16_t message_id, service_id, flags = 0;
    uint8_t tx_alloc = 0;

    /* special case for HTC pseudo control service */
    if (conn_req->service_id == ATH10K_HTC_SVC_ID_RSVD_CTRL) {
        disable_credit_flow_ctrl = true;
        assigned_eid = ATH10K_HTC_EP_0;
        max_msg_size = ATH10K_HTC_MAX_CTRL_MSG_LEN;
        memset(&resp_msg_dummy, 0, sizeof(resp_msg_dummy));
        goto setup;
    }

    tx_alloc = ath10k_htc_get_credit_allocation(htc, conn_req->service_id);
    if (!tx_alloc) {
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot htc service %s does not allocate target credits\n",
                   htc_service_name(conn_req->service_id));
    }

    status = ath10k_msg_buf_alloc(htc->ar, &msg_buf, ATH10K_MSG_TYPE_HTC_CONN_SVC, 0);
    if (status != ZX_OK) {
        return status;
    }

    msg = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC_MSG);
    msg->hdr.message_id = ATH10K_HTC_MSG_CONNECT_SERVICE_ID;

    flags |= SM(tx_alloc, ATH10K_HTC_CONN_FLAGS_RECV_ALLOC);

    /* Only enable credit flow control for WMI ctrl service */
    if (conn_req->service_id != ATH10K_HTC_SVC_ID_WMI_CONTROL) {
        flags |= ATH10K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL;
        disable_credit_flow_ctrl = true;
    }

    req_msg = &msg->connect_service;
    req_msg->flags = flags;
    req_msg->service_id = conn_req->service_id;

    completion_reset(&htc->ctl_resp);

    status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, msg_buf);
    if (status != ZX_OK) {
        ath10k_err("Failed to send connection request: %s\n", zx_status_get_string(status));
        ath10k_msg_buf_free(msg_buf);
        return status;
    }

    /* wait for response */
    status = ath10k_htc_wait_ctl_resp(htc);
    if (status != ZX_OK) {
        ath10k_err("Service connect error: %s\n", zx_status_get_string(status));
        ath10k_msg_buf_free(msg_buf);
        return status;
    }

    /* we controlled the buffer creation, it's aligned */
    msg = (struct ath10k_htc_msg*)htc->control_resp_buffer;
    resp_msg = &msg->connect_service_response;
    message_id = msg->hdr.message_id;
    service_id = resp_msg->service_id;

    resp_length = sizeof(msg->hdr) + sizeof(msg->connect_service_response);
    if ((message_id != ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID) ||
            (htc->control_resp_len < resp_length)) {
        ath10k_err("Invalid resp message ID 0x%x", message_id);
        return ZX_ERR_BAD_STATE;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTC,
               "HTC Service %s connect response: status: 0x%x, assigned ep: 0x%x\n",
               htc_service_name(service_id),
               resp_msg->status, resp_msg->eid);

    conn_resp->connect_resp_code = resp_msg->status;

    /* check response status */
    if (resp_msg->status != ATH10K_HTC_CONN_SVC_STATUS_SUCCESS) {
        ath10k_err("HTC Service %s connect request failed: 0x%x)\n",
                   htc_service_name(service_id),
                   resp_msg->status);
        return ZX_ERR_BAD_STATE;
    }

    assigned_eid = (enum ath10k_htc_ep_id)resp_msg->eid;
    max_msg_size = resp_msg->max_msg_size;

setup:

    if (assigned_eid >= ATH10K_HTC_EP_COUNT) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    if (max_msg_size == 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    ep = &htc->endpoint[assigned_eid];
    ep->eid = assigned_eid;

    if (ep->service_id != ATH10K_HTC_SVC_ID_UNUSED) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    /* return assigned endpoint to caller */
    conn_resp->eid = assigned_eid;
    conn_resp->max_msg_len = resp_msg->max_msg_size;

    /* setup the endpoint */
    ep->service_id = conn_req->service_id;
    ep->max_tx_queue_depth = conn_req->max_send_queue_depth;
    ep->max_ep_message_len = resp_msg->max_msg_size;
    ep->tx_credits = tx_alloc;

    /* copy all the callbacks */
    ep->ep_ops = conn_req->ep_ops;

    status = ath10k_hif_map_service_to_pipe(htc->ar,
                                            ep->service_id,
                                            &ep->ul_pipe_id,
                                            &ep->dl_pipe_id);
    if (status != ZX_OK) {
        return status;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT,
               "boot htc service '%s' ul pipe %d dl pipe %d eid %d ready\n",
               htc_service_name(ep->service_id), ep->ul_pipe_id,
               ep->dl_pipe_id, ep->eid);

    if (disable_credit_flow_ctrl && ep->tx_credit_flow_enabled) {
        ep->tx_credit_flow_enabled = false;
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot htc service '%s' eid %d TX flow control disabled\n",
                   htc_service_name(ep->service_id), assigned_eid);
    }

    return status;
}

zx_status_t ath10k_htc_start(struct ath10k_htc* htc) {
    struct ath10k* ar = htc->ar;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;
    struct ath10k_htc_msg* msg;

    status = ath10k_msg_buf_alloc(htc->ar, &msg_buf, ATH10K_MSG_TYPE_HTC_SETUP_COMPLETE_EXT, 0);
    if (status != ZX_OK) {
        return status;
    }

    msg = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTC_MSG);
    msg->hdr.message_id = ATH10K_HTC_MSG_SETUP_COMPLETE_EX_ID;

    if (ar->hif.bus == ATH10K_BUS_SDIO) {
        /* Extra setup params used by SDIO */
        msg->setup_complete_ext.flags =
            ATH10K_HTC_SETUP_COMPLETE_FLAGS_RX_BNDL_EN;
        msg->setup_complete_ext.max_msgs_per_bundled_recv =
            htc->max_msgs_per_htc_bundle;
    }
    ath10k_dbg(ar, ATH10K_DBG_HTC, "HTC is using TX credit flow control\n");

    status = ath10k_htc_send(htc, ATH10K_HTC_EP_0, msg_buf);
    if (status != ZX_OK) {
        ath10k_msg_buf_free(msg_buf);
        return status;
    }

    return ZX_OK;
}

/* registered target arrival callback from the HIF layer */
zx_status_t ath10k_htc_init(struct ath10k* ar) {
    zx_status_t status;
    struct ath10k_htc* htc = &ar->htc;
    struct ath10k_htc_svc_conn_req conn_req;
    struct ath10k_htc_svc_conn_resp conn_resp;

    mtx_init(&htc->tx_lock, mtx_plain);

    ath10k_htc_reset_endpoint_states(htc);

    htc->ar = ar;

    /* setup our pseudo HTC control endpoint connection */
    memset(&conn_req, 0, sizeof(conn_req));
    memset(&conn_resp, 0, sizeof(conn_resp));
    conn_req.ep_ops.ep_tx_complete = ath10k_htc_control_tx_complete;
    conn_req.ep_ops.ep_rx_complete = ath10k_htc_control_rx_complete;
    conn_req.max_send_queue_depth = ATH10K_NUM_CONTROL_TX_BUFFERS;
    conn_req.service_id = ATH10K_HTC_SVC_ID_RSVD_CTRL;

    /* connect fake service */
    status = ath10k_htc_connect_service(htc, &conn_req, &conn_resp);
    if (status != ZX_OK) {
        ath10k_err("could not connect to htc service (%d)\n",
                   status);
        return status;
    }

    htc->ctl_resp = COMPLETION_INIT;

    return ZX_OK;
}
