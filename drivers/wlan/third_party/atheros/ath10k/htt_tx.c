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

#include <assert.h>

#include "htt.h"
#include "ieee80211.h"
#include "mac.h"
#include "hif.h"
#include "txrx.h"
#include "debug.h"

#if 0 // NEEDS PORTING
static uint8_t ath10k_htt_tx_txq_calc_size(size_t count) {
    int exp;
    int factor;

    exp = 0;
    factor = count >> 7;

    while (factor >= 64 && exp < 4) {
        factor >>= 3;
        exp++;
    }

    if (exp == 4) {
        return 0xff;
    }

    if (count > 0) {
        factor = max(1, factor);
    }

    return SM(exp, HTT_TX_Q_STATE_ENTRY_EXP) |
           SM(factor, HTT_TX_Q_STATE_ENTRY_FACTOR);
}

static void __ath10k_htt_tx_txq_recalc(struct ieee80211_hw* hw,
                                       struct ieee80211_txq* txq) {
    struct ath10k* ar = hw->priv;
    struct ath10k_sta* arsta;
    struct ath10k_vif* arvif = (void*)txq->vif->drv_priv;
    unsigned long frame_cnt;
    unsigned long byte_cnt;
    int idx;
    uint32_t bit;
    uint16_t peer_id;
    uint8_t tid;
    uint8_t count;

    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    if (!ar->htt.tx_q_state.enabled) {
        return;
    }

    if (ar->htt.tx_q_state.mode != HTT_TX_MODE_SWITCH_PUSH_PULL) {
        return;
    }

    if (txq->sta) {
        arsta = (void*)txq->sta->drv_priv;
        peer_id = arsta->peer_id;
    } else {
        peer_id = arvif->peer_id;
    }

    tid = txq->tid;
    bit = BIT(peer_id % 32);
    idx = peer_id / 32;

    ieee80211_txq_get_depth(txq, &frame_cnt, &byte_cnt);
    count = ath10k_htt_tx_txq_calc_size(byte_cnt);

    if (unlikely(peer_id >= ar->htt.tx_q_state.num_peers) ||
            unlikely(tid >= ar->htt.tx_q_state.num_tids)) {
        ath10k_warn("refusing to update txq for peer_id %hu tid %hhu due to out of bounds\n",
                    peer_id, tid);
        return;
    }

    ar->htt.tx_q_state.vaddr->count[tid][peer_id] = count;
    ar->htt.tx_q_state.vaddr->map[tid][idx] &= ~bit;
    ar->htt.tx_q_state.vaddr->map[tid][idx] |= count ? bit : 0;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx txq state update peer_id %hu tid %hhu count %hhu\n",
               peer_id, tid, count);
}

static void __ath10k_htt_tx_txq_sync(struct ath10k* ar) {
    uint32_t seq;
    size_t size;

    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    if (!ar->htt.tx_q_state.enabled) {
        return;
    }

    if (ar->htt.tx_q_state.mode != HTT_TX_MODE_SWITCH_PUSH_PULL) {
        return;
    }

    seq = ar->htt.tx_q_state.vaddr->seq;
    seq++;
    ar->htt.tx_q_state.vaddr->seq = seq;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx txq state update commit seq %u\n",
               seq);

    size = sizeof(*ar->htt.tx_q_state.vaddr);
    dma_sync_single_for_device(ar->dev,
                               ar->htt.tx_q_state.paddr,
                               size,
                               DMA_TO_DEVICE);
}

void ath10k_htt_tx_txq_recalc(struct ieee80211_hw* hw,
                              struct ieee80211_txq* txq) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->htt.tx_lock);
    __ath10k_htt_tx_txq_recalc(hw, txq);
    mtx_unlock(&ar->htt.tx_lock);
}

void ath10k_htt_tx_txq_sync(struct ath10k* ar) {
    mtx_lock(&ar->htt.tx_lock);
    __ath10k_htt_tx_txq_sync(ar);
    mtx_unlock(&ar->htt.tx_lock);
}

void ath10k_htt_tx_txq_update(struct ieee80211_hw* hw,
                              struct ieee80211_txq* txq) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->htt.tx_lock);
    __ath10k_htt_tx_txq_recalc(hw, txq);
    __ath10k_htt_tx_txq_sync(ar);
    mtx_unlock(&ar->htt.tx_lock);
}
#endif // NEEDS PORTING

void ath10k_htt_tx_dec_pending(struct ath10k_htt* htt) {
    ASSERT_MTX_HELD(&htt->tx_lock);

    htt->num_pending_tx--;
#if 0 // NEEDS PORTING
    if (htt->num_pending_tx == htt->max_num_pending_tx - 1) {
        ath10k_mac_tx_unlock(htt->ar, ATH10K_TX_PAUSE_Q_FULL);
    }
#endif // NEEDS PORTING
}

zx_status_t ath10k_htt_tx_inc_pending(struct ath10k_htt* htt) {
    ASSERT_MTX_HELD(&htt->tx_lock);

    if (htt->num_pending_tx >= htt->max_num_pending_tx) {
        // Don't return ZX_ERR_SHOULD_WAIT here, that has a special meaning to the
        // queue_tx caller.
        return ZX_ERR_NO_RESOURCES;
    }

    htt->num_pending_tx++;
#if 0 // NEEDS PORTING
    if (htt->num_pending_tx == htt->max_num_pending_tx) {
        ath10k_mac_tx_lock(htt->ar, ATH10K_TX_PAUSE_Q_FULL);
    }
#endif // NEEDS PORTING

    return ZX_OK;
}

zx_status_t ath10k_htt_tx_mgmt_inc_pending(struct ath10k_htt* htt, bool is_mgmt, bool is_presp) {
    struct ath10k* ar = htt->ar;

    ASSERT_MTX_HELD(&htt->tx_lock);

    if (!is_mgmt || !ar->hw_params.max_probe_resp_desc_thres) {
        return ZX_OK;
    }

    if (is_presp && (int)ar->hw_params.max_probe_resp_desc_thres < htt->num_pending_mgmt_tx) {
        return ZX_ERR_SHOULD_WAIT;
    }

    htt->num_pending_mgmt_tx++;

    return ZX_OK;
}

void ath10k_htt_tx_mgmt_dec_pending(struct ath10k_htt* htt) {
    ASSERT_MTX_HELD(&htt->tx_lock);

    if (!htt->ar->hw_params.max_probe_resp_desc_thres) {
        return;
    }

    htt->num_pending_mgmt_tx--;
}

zx_status_t ath10k_htt_tx_alloc_msdu_id(struct ath10k_htt* htt,
                                        struct ath10k_msg_buf* buf,
                                        ssize_t* id_ptr) {
    struct ath10k* ar = htt->ar;
    ssize_t id;

    ASSERT_MTX_HELD(&htt->tx_lock);

    id = sa_add(htt->pending_tx, buf);

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx alloc msdu_id %d\n", id);

    if (id < 0) {
        return ZX_ERR_NO_RESOURCES;
    }

    *id_ptr = id;
    return ZX_OK;
}

void ath10k_htt_tx_free_msdu_id(struct ath10k_htt* htt, uint16_t msdu_id) {
    struct ath10k* ar = htt->ar;

    ASSERT_MTX_HELD(&htt->tx_lock);

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx free msdu_id %hu\n", msdu_id);

    sa_remove(htt->pending_tx, msdu_id);
}

static void ath10k_htt_tx_free_cont_txbuf(struct ath10k_htt* htt) {
    if (!io_buffer_is_valid(&htt->txbuf.handle)) {
        return;
    }

    io_buffer_release(&htt->txbuf.handle);
    htt->txbuf.vaddr = NULL;
    htt->txbuf.paddr = 0;
}

static zx_status_t ath10k_htt_tx_alloc_cont_txbuf(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    size_t size;
    zx_handle_t bti_handle;
    zx_status_t ret;

    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) {
        return ret;
    }

    size = htt->max_num_pending_tx * sizeof(struct ath10k_htt_txbuf);
    ret = io_buffer_init(&htt->txbuf.handle, bti_handle, size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        return ret;
    }
    htt->txbuf.vaddr = io_buffer_virt(&htt->txbuf.handle);
    htt->txbuf.paddr = io_buffer_phys(&htt->txbuf.handle);
    if (htt->txbuf.paddr + size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        io_buffer_release(&htt->txbuf.handle);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static void ath10k_htt_tx_free_cont_frag_desc(struct ath10k_htt* htt) {
    if (!io_buffer_is_valid(&htt->frag_desc.handle)) {
        return;
    }

    io_buffer_release(&htt->frag_desc.handle);
    htt->frag_desc.vaddr = NULL;
    htt->frag_desc.paddr = 0;
}

static zx_status_t ath10k_htt_tx_alloc_cont_frag_desc(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    zx_handle_t bti_handle;
    zx_status_t ret;
    size_t size;

    if (!ar->hw_params.continuous_frag_desc) {
        return 0;
    }

    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) {
        return ret;
    }

    size = htt->max_num_pending_tx * sizeof(struct htt_msdu_ext_desc);
    ret = io_buffer_init(&htt->frag_desc.handle, bti_handle, size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        return ret;
    }
    htt->frag_desc.vaddr = io_buffer_virt(&htt->frag_desc.handle);
    htt->frag_desc.paddr = io_buffer_phys(&htt->frag_desc.handle);
    if (htt->frag_desc.paddr + size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        io_buffer_release(&htt->frag_desc.handle);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static void ath10k_htt_tx_free_txq(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;

    if (!BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_PEER_FLOW_CONTROL)) {
        return;
    }

    io_buffer_release(&htt->tx_q_state.handle);
}

static zx_status_t ath10k_htt_tx_alloc_txq(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    zx_handle_t bti_handle;
    size_t size;
    zx_status_t ret;

    if (!(BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_PEER_FLOW_CONTROL))) {
        return ZX_OK;
    }

    htt->tx_q_state.num_peers = HTT_TX_Q_STATE_NUM_PEERS;
    htt->tx_q_state.num_tids = HTT_TX_Q_STATE_NUM_TIDS;
    htt->tx_q_state.type = HTT_Q_DEPTH_TYPE_BYTES;

    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) {
        return ret;
    }

    size = sizeof(*htt->tx_q_state.vaddr);
    ret = io_buffer_init(&htt->tx_q_state.handle, bti_handle, size,
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        return ret;
    }
    htt->tx_q_state.vaddr = io_buffer_virt(&htt->tx_q_state.handle);
    htt->tx_q_state.paddr = io_buffer_phys(&htt->tx_q_state.handle);
    if (htt->tx_q_state.paddr + size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        io_buffer_release(&htt->tx_q_state.handle);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static void ath10k_htt_tx_free_txdone_fifo(struct ath10k_htt* htt) {
    COND_WARN(!kfifo_is_empty(&htt->txdone_fifo));
    kfifo_free(&htt->txdone_fifo);
}

static int ath10k_htt_tx_alloc_txdone_fifo(struct ath10k_htt* htt) {
    int ret;
    size_t size;

    size = ROUNDUP_POW2(htt->max_num_pending_tx);
    ret = kfifo_alloc(&htt->txdone_fifo, size, GFP_KERNEL);
    return ret;
}
#endif // NEEDS PORTING

static zx_status_t ath10k_htt_tx_alloc_buf(struct ath10k_htt* htt) {
    zx_status_t ret;

    ret = ath10k_htt_tx_alloc_cont_txbuf(htt);
    if (ret) {
        ath10k_err("failed to alloc cont tx buffer: %d\n", ret);
        return ret;
    }

    ret = ath10k_htt_tx_alloc_cont_frag_desc(htt);
    if (ret) {
        ath10k_err("failed to alloc cont frag desc: %d\n", ret);
        goto free_txbuf;
    }

    ret = ath10k_htt_tx_alloc_txq(htt);
    if (ret) {
        ath10k_err("failed to alloc txq: %d\n", ret);
        goto free_frag_desc;
    }

#if 0 // NEEDS PORTING
    ret = ath10k_htt_tx_alloc_txdone_fifo(htt);
    if (ret) {
        ath10k_err("failed to alloc txdone fifo: %d\n", ret);
        goto free_txq;
    }
#endif // NEEDS PORTING

    return ZX_OK;

#if 0 // NEEDS PORTING
free_txq:
#endif // NEEDS PORTING
    ath10k_htt_tx_free_txq(htt);

free_frag_desc:
    ath10k_htt_tx_free_cont_frag_desc(htt);

free_txbuf:
    ath10k_htt_tx_free_cont_txbuf(htt);

    return ret;
}

zx_status_t ath10k_htt_tx_start(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "htt tx max num pending tx %d\n",
               htt->max_num_pending_tx);

    mtx_init(&htt->tx_lock, mtx_plain);
    sa_init(&htt->pending_tx, htt->max_num_pending_tx);

    if (htt->tx_mem_allocated) {
        return ZX_OK;
    }

    ret = ath10k_htt_tx_alloc_buf(htt);
    if (ret) {
        goto free_sa_pending_tx;
    }

    htt->tx_mem_allocated = true;

    return ZX_OK;

free_sa_pending_tx:
    sa_free(htt->pending_tx);

    return ret;
}

static void ath10k_htt_tx_clean_up_pending(ssize_t ndx, void* skb, void* ctx) {
    uint16_t msdu_id = ndx;
    struct ath10k* ar = ctx;
    struct ath10k_htt* htt = &ar->htt;
    struct htt_tx_done tx_done = {0};

    ath10k_dbg(ar, ATH10K_DBG_HTT, "force cleanup msdu_id %hu\n", msdu_id);

    tx_done.msdu_id = msdu_id;
    tx_done.status = HTT_TX_COMPL_STATE_DISCARD;

    ath10k_txrx_tx_unref(htt, &tx_done);
}

void ath10k_htt_tx_destroy(struct ath10k_htt* htt) {
    if (!htt->tx_mem_allocated) {
        return;
    }

    ath10k_htt_tx_free_cont_txbuf(htt);
    ath10k_htt_tx_free_txq(htt);
    ath10k_htt_tx_free_cont_frag_desc(htt);
#if 0 // NEEDS PORTING
    ath10k_htt_tx_free_txdone_fifo(htt);
#endif // NEEDS PORTING
    htt->tx_mem_allocated = false;
}

void ath10k_htt_tx_stop(struct ath10k_htt* htt) {
    sa_for_each(htt->pending_tx, ath10k_htt_tx_clean_up_pending, htt->ar);
    sa_free(htt->pending_tx);
}

void ath10k_htt_tx_free(struct ath10k_htt* htt) {
    ath10k_htt_tx_stop(htt);
    ath10k_htt_tx_destroy(htt);
}

void ath10k_htt_htc_tx_complete(struct ath10k* ar, struct ath10k_msg_buf* buff) {
    ath10k_msg_buf_free(buff);
}

void ath10k_htt_hif_tx_complete(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    ath10k_msg_buf_free(msg_buf);
}

zx_status_t ath10k_htt_h2t_ver_req_msg(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t ret;

    ret = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_HTT_CMD_VER_REQ, 0);
    if (ret != ZX_OK) {
        return ret;
    }

    struct htt_cmd_hdr* cmd_hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD);
    cmd_hdr->msg_type = HTT_H2T_MSG_TYPE_VERSION_REQ;

    ret = ath10k_htc_send(&htt->ar->htc, htt->eid, msg_buf);
    if (ret != ZX_OK) {
        ath10k_msg_buf_free(msg_buf);
        return ret;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
int ath10k_htt_h2t_stats_req(struct ath10k_htt* htt, uint8_t mask, uint64_t cookie) {
    struct ath10k* ar = htt->ar;
    struct htt_stats_req* req;
    struct sk_buff* skb;
    struct htt_cmd* cmd;
    int len = 0, ret;

    len += sizeof(cmd->hdr);
    len += sizeof(cmd->stats_req);

    skb = ath10k_htc_alloc_skb(ar, len);
    if (!skb) {
        return -ENOMEM;
    }

    skb_put(skb, len);
    cmd = (struct htt_cmd*)skb->data;
    cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_STATS_REQ;

    req = &cmd->stats_req;

    memset(req, 0, sizeof(*req));

    /* currently we support only max 8 bit masks so no need to worry
     * about endian support
     */
    req->upload_types[0] = mask;
    req->reset_types[0] = mask;
    req->stat_type = HTT_STATS_REQ_CFG_STAT_TYPE_INVALID;
    req->cookie_lsb = cookie & 0xffffffff;
    req->cookie_msb = (cookie & 0xffffffff00000000ULL) >> 32;

    ret = ath10k_htc_send(&htt->ar->htc, htt->eid, skb);
    if (ret) {
        ath10k_warn("failed to send htt type stats request: %d",
                    ret);
        dev_kfree_skb_any(skb);
        return ret;
    }

    return 0;
}
#endif // NEEDS PORTING

zx_status_t ath10k_htt_send_frag_desc_bank_cfg(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    struct ath10k_msg_buf* msg_buf;
    struct htt_cmd_hdr* cmd_hdr;
    struct htt_frag_desc_bank_cfg* cfg;
    zx_status_t ret;
    uint8_t info;

    if (!ar->hw_params.continuous_frag_desc) {
        return ZX_OK;
    }

    if (!htt->frag_desc.paddr) {
        ath10k_warn("invalid frag desc memory\n");
        return ZX_ERR_BAD_STATE;
    }

    ret = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_HTT_CMD_FRAG_DESC_BANK_CFG, 0);
    if (ret != ZX_OK) {
        return ret;
    }

    cmd_hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD);
    cmd_hdr->msg_type = HTT_H2T_MSG_TYPE_FRAG_DESC_BANK_CFG;

    info = 0;
    info |= SM(htt->tx_q_state.type,
               HTT_FRAG_DESC_BANK_CFG_INFO_Q_STATE_DEPTH_TYPE);

    if (BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_PEER_FLOW_CONTROL)) {
        info |= HTT_FRAG_DESC_BANK_CFG_INFO_Q_STATE_VALID;
    }

    cfg = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD_FRAG_DESC_BANK_CFG);
    cfg->info = info;
    cfg->num_banks = 1;
    cfg->desc_size = sizeof(struct htt_msdu_ext_desc);
    cfg->bank_base_addrs[0] = htt->frag_desc.paddr;
    cfg->bank_id[0].bank_min_id = 0;
    cfg->bank_id[0].bank_max_id = htt->max_num_pending_tx - 1;

    cfg->q_state.paddr = htt->tx_q_state.paddr;
    cfg->q_state.num_peers = htt->tx_q_state.num_peers;
    cfg->q_state.num_tids = htt->tx_q_state.num_tids;
    cfg->q_state.record_size = HTT_TX_Q_STATE_ENTRY_SIZE;
    cfg->q_state.record_multiplier = HTT_TX_Q_STATE_ENTRY_MULTIPLIER;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt frag desc bank cmd\n");

    ret = ath10k_htc_send(&htt->ar->htc, htt->eid, msg_buf);
    if (ret != ZX_OK) {
        ath10k_warn("failed to send frag desc bank cfg request: %s\n",
                    zx_status_get_string(ret));
        ath10k_msg_buf_free(msg_buf);
        return ret;
    }

    return ZX_OK;
}

zx_status_t ath10k_htt_send_rx_ring_cfg_ll(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    struct ath10k_msg_buf* msg_buf;
    struct htt_cmd* cmd;
    struct htt_rx_ring_setup_ring* ring;
    uint16_t flags;
    uint32_t fw_idx;
    zx_status_t ret;

    /*
     * the HW expects the buffer to be an integral number of 4-byte
     * "words"
     */
    static_assert(IS_ALIGNED(HTT_RX_BUF_SIZE, 4),
                  "Rx ring buffer size must be an increment of 4 bytes");
    static_assert((HTT_RX_BUF_SIZE & HTT_MAX_CACHE_LINE_SIZE_MASK) == 0,
                  "Rx ring buffer insufficiently aligned");

    size_t extra = sizeof(struct htt_rx_ring_setup_ring);
    ret = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_HTT_CMD_RX_SETUP, extra);
    if (ret != ZX_OK) {
        return ret;
    }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD);
    ring = cmd->rx_setup.rings;

    cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_RX_RING_CFG;
    cmd->rx_setup.hdr.num_rings = 1;

    /* FIXME: do we need all of this? */
    flags = 0;
    flags |= HTT_RX_RING_FLAGS_MAC80211_HDR;
    flags |= HTT_RX_RING_FLAGS_MSDU_PAYLOAD;
    flags |= HTT_RX_RING_FLAGS_PPDU_START;
    flags |= HTT_RX_RING_FLAGS_PPDU_END;
    flags |= HTT_RX_RING_FLAGS_MPDU_START;
    flags |= HTT_RX_RING_FLAGS_MPDU_END;
    flags |= HTT_RX_RING_FLAGS_MSDU_START;
    flags |= HTT_RX_RING_FLAGS_MSDU_END;
    flags |= HTT_RX_RING_FLAGS_RX_ATTENTION;
    flags |= HTT_RX_RING_FLAGS_FRAG_INFO;
    flags |= HTT_RX_RING_FLAGS_UNICAST_RX;
    flags |= HTT_RX_RING_FLAGS_MULTICAST_RX;
    flags |= HTT_RX_RING_FLAGS_CTRL_RX;
    flags |= HTT_RX_RING_FLAGS_MGMT_RX;
    flags |= HTT_RX_RING_FLAGS_NULL_RX;
    flags |= HTT_RX_RING_FLAGS_PHY_DATA_RX;

    fw_idx = *htt->rx_ring.alloc_idx.vaddr;

    ring->fw_idx_shadow_reg_paddr =
        htt->rx_ring.alloc_idx.paddr;
    ring->rx_ring_base_paddr = htt->rx_ring.base_paddr;
    ring->rx_ring_len = htt->rx_ring.size;
    ring->rx_ring_bufsize = HTT_RX_BUF_SIZE;
    ring->flags = flags;
    ring->fw_idx_init_val = fw_idx;

#define desc_offset(x) (offsetof(struct htt_rx_desc, x) / 4)

    ring->mac80211_hdr_offset = desc_offset(rx_hdr_status);
    ring->msdu_payload_offset = desc_offset(msdu_payload);
    ring->ppdu_start_offset = desc_offset(ppdu_start);
    ring->ppdu_end_offset = desc_offset(ppdu_end);
    ring->mpdu_start_offset = desc_offset(mpdu_start);
    ring->mpdu_end_offset = desc_offset(mpdu_end);
    ring->msdu_start_offset = desc_offset(msdu_start);
    ring->msdu_end_offset = desc_offset(msdu_end);
    ring->rx_attention_offset = desc_offset(attention);
    ring->frag_info_offset = desc_offset(frag_info);

#undef desc_offset

    ret = ath10k_htc_send(&htt->ar->htc, htt->eid, msg_buf);
    if (ret != ZX_OK) {
        ath10k_msg_buf_free(msg_buf);
        return ret;
    }

    return ZX_OK;
}

zx_status_t ath10k_htt_h2t_aggr_cfg_msg(struct ath10k_htt* htt,
                                        uint8_t max_subfrms_ampdu,
                                        uint8_t max_subfrms_amsdu) {
    struct ath10k* ar = htt->ar;
    struct htt_aggr_conf* aggr_conf;
    struct ath10k_msg_buf* msg_buf;
    struct htt_cmd* cmd;
    zx_status_t ret;

    /* Firmware defaults are: amsdu = 3 and ampdu = 64 */

    if (max_subfrms_ampdu == 0 || max_subfrms_ampdu > 64) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (max_subfrms_amsdu == 0 || max_subfrms_amsdu > 31) {
        return ZX_ERR_INVALID_ARGS;
    }

    ret = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_HTT_CMD_AGGR_CONF, 0);
    if (ret != ZX_OK) {
        return ret;
    }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD);;
    cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_AGGR_CFG;

    aggr_conf = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_CMD_AGGR_CONF);
    aggr_conf->max_num_ampdu_subframes = max_subfrms_ampdu;
    aggr_conf->max_num_amsdu_subframes = max_subfrms_amsdu;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt h2t aggr cfg msg amsdu %d ampdu %d",
               aggr_conf->max_num_amsdu_subframes,
               aggr_conf->max_num_ampdu_subframes);

    ret = ath10k_htc_send(&htt->ar->htc, htt->eid, msg_buf);
    if (ret != ZX_OK) {
        ath10k_msg_buf_free(msg_buf);
        return ret;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
int ath10k_htt_tx_fetch_resp(struct ath10k* ar,
                             uint32_t token,
                             uint16_t fetch_seq_num,
                             struct htt_tx_fetch_record* records,
                             size_t num_records) {
    struct sk_buff* skb;
    struct htt_cmd* cmd;
    const uint16_t resp_id = 0;
    int len = 0;
    int ret;

    /* Response IDs are echo-ed back only for host driver convienence
     * purposes. They aren't used for anything in the driver yet so use 0.
     */

    len += sizeof(cmd->hdr);
    len += sizeof(cmd->tx_fetch_resp);
    len += sizeof(cmd->tx_fetch_resp.records[0]) * num_records;

    skb = ath10k_htc_alloc_skb(ar, len);
    if (!skb) {
        return -ENOMEM;
    }

    skb_put(skb, len);
    cmd = (struct htt_cmd*)skb->data;
    cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_TX_FETCH_RESP;
    cmd->tx_fetch_resp.resp_id = resp_id;
    cmd->tx_fetch_resp.fetch_seq_num = fetch_seq_num;
    cmd->tx_fetch_resp.num_records = num_records;
    cmd->tx_fetch_resp.token = token;

    memcpy(cmd->tx_fetch_resp.records, records,
           sizeof(records[0]) * num_records);

    ret = ath10k_htc_send(&ar->htc, ar->htt.eid, skb);
    if (ret) {
        ath10k_warn("failed to submit htc command: %d\n", ret);
        goto err_free_skb;
    }

    return 0;

err_free_skb:
    dev_kfree_skb_any(skb);

    return ret;
}
#endif // NEEDS PORTING

static uint8_t ath10k_htt_tx_get_vdev_id(struct ath10k* ar) {
    struct ath10k_vif* arvif = &ar->arvif;

#if 0 // NEEDS PORTING
    if (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) {
        return ar->scan.vdev_id;
    } else
#endif // NEEDS PORTING
    return arvif->vdev_id;
}

static uint8_t ath10k_htt_tx_get_tid(struct ath10k_msg_buf* tx_buf, bool is_eth) {
    struct ieee80211_frame_header* hdr = ath10k_msg_buf_get_payload(tx_buf);
    if (!is_eth && (ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_MGMT)) {
        return HTT_DATA_TX_EXT_TID_MGMT;
    } else if (tx_buf->tx.flags & ATH10K_TX_BUF_QOS) {
        // TODO: priority % IEEE80211_QOS_CTL_TID_MASK
        return 0;
    } else {
        return HTT_DATA_TX_EXT_TID_NON_QOS_MCAST_BCAST;
    }
}

zx_status_t ath10k_htt_mgmt_tx(struct ath10k_htt* htt, struct ath10k_msg_buf* tx_buf) {
ath10k_err("ath10k_htt_mgmt_tx unimplemented - dropping tx packet!\n");
#if 0 // NEEDS PORTING
    struct ath10k* ar = htt->ar;
    struct device* dev = ar->dev;
    struct sk_buff* txdesc = NULL;
    struct htt_cmd* cmd;
    struct ath10k_skb_cb* skb_cb = ATH10K_SKB_CB(msdu);
    uint8_t vdev_id = ath10k_htt_tx_get_vdev_id(ar, msdu);
    int len = 0;
    int msdu_id = -1;
    int res;
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)msdu->data;

    len += sizeof(cmd->hdr);
    len += sizeof(cmd->mgmt_tx);

    mtx_lock(&htt->tx_lock);
    res = ath10k_htt_tx_alloc_msdu_id(htt, msdu);
    mtx_unlock(&htt->tx_lock);
    if (res < 0) {
        goto err;
    }

    msdu_id = res;

    if ((ieee80211_is_action(hdr->frame_control) ||
            ieee80211_is_deauth(hdr->frame_control) ||
            ieee80211_is_disassoc(hdr->frame_control)) &&
            ieee80211_has_protected(hdr->frame_control)) {
        skb_put(msdu, IEEE80211_CCMP_MIC_LEN);
    }

    txdesc = ath10k_htc_alloc_skb(ar, len);
    if (!txdesc) {
        res = -ENOMEM;
        goto err_free_msdu_id;
    }

    skb_cb->paddr = dma_map_single(dev, msdu->data, msdu->len,
                                   DMA_TO_DEVICE);
    res = dma_mapping_error(dev, skb_cb->paddr);
    if (res) {
        res = -EIO;
        goto err_free_txdesc;
    }

    skb_put(txdesc, len);
    cmd = (struct htt_cmd*)txdesc->data;
    memset(cmd, 0, len);

    cmd->hdr.msg_type         = HTT_H2T_MSG_TYPE_MGMT_TX;
    cmd->mgmt_tx.msdu_paddr = ATH10K_SKB_CB(msdu->paddr);
    cmd->mgmt_tx.len        = msdu->len;
    cmd->mgmt_tx.desc_id    = msdu_id;
    cmd->mgmt_tx.vdev_id    = vdev_id;
    memcpy(cmd->mgmt_tx.hdr, msdu->data,
           MIN_T(int, msdu->len, HTT_MGMT_FRM_HDR_DOWNLOAD_LEN));

    res = ath10k_htc_send(&htt->ar->htc, htt->eid, txdesc);
    if (res) {
        goto err_unmap_msdu;
    }

    return 0;

err_unmap_msdu:
    dma_unmap_single(dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
err_free_txdesc:
    dev_kfree_skb_any(txdesc);
err_free_msdu_id:
    mtx_lock(&htt->tx_lock);
    ath10k_htt_tx_free_msdu_id(htt, msdu_id);
    mtx_unlock(&htt->tx_lock);
err:
    return res;
#endif // NEEDS PORTING
return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ath10k_htt_tx(struct ath10k_htt* htt,
                          enum ath10k_hw_txrx_mode txmode,
                          struct ath10k_msg_buf* msdu) {
    struct ath10k* ar = htt->ar;
    struct ath10k_hif_sg_item sg_items[2];
    struct htt_data_tx_desc_frag* frags;
    bool is_eth = (txmode == ATH10K_HW_TXRX_ETHERNET);
    uint8_t vdev_id = ath10k_htt_tx_get_vdev_id(ar);
    uint8_t tid = ath10k_htt_tx_get_tid(msdu, is_eth);
    uint8_t flags0 = 0;
    uint16_t flags1 = 0;
    uint16_t freq = 0;
    uint32_t frags_paddr = 0;
    struct htt_msdu_ext_desc* ext_desc = NULL;
    zx_status_t ret;
    ssize_t id;

    mtx_lock(&htt->tx_lock);
    ret = ath10k_htt_tx_alloc_msdu_id(htt, msdu, &id);
    mtx_unlock(&htt->tx_lock);
    if (ret != ZX_OK) {
        goto err;
    }

    uint16_t msdu_id = id;

    int prefetch_len = MIN(htt->prefetch_len, msdu->used);
    prefetch_len = ROUNDUP(prefetch_len, 4);

    struct ath10k_htt_txbuf* txbuf = &htt->txbuf.vaddr[msdu_id];
    uint32_t txbuf_paddr = htt->txbuf.paddr
                           + (sizeof(struct ath10k_htt_txbuf) * msdu_id);

    struct ieee80211_frame_header* hdr = ath10k_msg_buf_get_payload(msdu);
    if ((ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_MGMT)
        && ((ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_ACTION)
            || (ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_DEAUTH)
            || (ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_DISASSOC))
        && (hdr->frame_ctrl & IEEE80211_FRAME_PROTECTED_MASK)) {
        msdu->used += IEEE80211_CCMP_MIC_LEN;
    } else if ((msdu->tx.flags & ATH10K_TX_BUF_PROTECTED) &&
               txmode == ATH10K_HW_TXRX_RAW &&
               (hdr->frame_ctrl & IEEE80211_FRAME_PROTECTED_MASK)) {
        msdu->used += IEEE80211_CCMP_MIC_LEN;
    }

#if 0 // NEEDS PORTING
    if (unlikely(info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)) {
        freq = ar->scan.roc_freq;
    }
#endif // NEEDS PORTING

    switch (txmode) {
    case ATH10K_HW_TXRX_RAW:
    case ATH10K_HW_TXRX_NATIVE_WIFI:
        flags0 |= HTT_DATA_TX_DESC_FLAGS0_MAC_HDR_PRESENT;
    /* pass through */
    case ATH10K_HW_TXRX_ETHERNET:
        if (ar->hw_params.continuous_frag_desc) {
            memset(&htt->frag_desc.vaddr[msdu_id], 0,
                   sizeof(struct htt_msdu_ext_desc));
            frags = (struct htt_data_tx_desc_frag*)
                    &htt->frag_desc.vaddr[msdu_id].frags;
            ext_desc = &htt->frag_desc.vaddr[msdu_id];
            frags[0].tword_addr.paddr_lo =
                msdu->paddr;
            frags[0].tword_addr.paddr_hi = 0;
            frags[0].tword_addr.len_16 = msdu->used;

            frags_paddr = htt->frag_desc.paddr
                          + (sizeof(struct htt_msdu_ext_desc) * msdu_id);
        } else {
            frags = txbuf->frags;
            frags[0].dword_addr.paddr =
                msdu->paddr;
            frags[0].dword_addr.len = msdu->used;
            frags[1].dword_addr.paddr = 0;
            frags[1].dword_addr.len = 0;

            frags_paddr = txbuf_paddr;
        }
        flags0 |= SM(txmode, HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE);
        break;
    case ATH10K_HW_TXRX_MGMT:
        flags0 |= SM(ATH10K_HW_TXRX_MGMT,
                     HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE);
        flags0 |= HTT_DATA_TX_DESC_FLAGS0_MAC_HDR_PRESENT;

        frags_paddr = msdu->paddr;
        break;
    }

    /* Normally all commands go through HTC which manages tx credits for
     * each endpoint and notifies when tx is completed.
     *
     * HTT endpoint is creditless so there's no need to care about HTC
     * flags. In that case it is trivial to fill the HTC header here.
     *
     * MSDU transmission is considered completed upon HTT event. This
     * implies no relevant resources can be freed until after the event is
     * received. That's why HTC tx completion handler itself is ignored by
     * setting NULL to transfer_context for all sg items.
     *
     * There is simply no point in pushing HTT TX_FRM through HTC tx path
     * as it's a waste of resources. By bypassing HTC it is possible to
     * avoid extra memory allocations, compress data structures and thus
     * improve performance.
     */

    txbuf->htc_hdr.eid = htt->eid;
    txbuf->htc_hdr.len = sizeof(txbuf->cmd_hdr) + sizeof(txbuf->cmd_tx) + prefetch_len;
    txbuf->htc_hdr.flags = 0;

    if (!(msdu->tx.flags & ATH10K_TX_BUF_PROTECTED)) {
        flags0 |= HTT_DATA_TX_DESC_FLAGS0_NO_ENCRYPT;
    }

    flags1 |= SM((uint16_t)vdev_id, HTT_DATA_TX_DESC_FLAGS1_VDEV_ID);
    flags1 |= SM((uint16_t)tid, HTT_DATA_TX_DESC_FLAGS1_EXT_TID);
#if 0 // NEEDS PORTING
    if (msdu->ip_summed == CHECKSUM_PARTIAL &&
            !BITARR_TEST(&ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
        flags1 |= HTT_DATA_TX_DESC_FLAGS1_CKSUM_L3_OFFLOAD;
        flags1 |= HTT_DATA_TX_DESC_FLAGS1_CKSUM_L4_OFFLOAD;
        if (ar->hw_params.continuous_frag_desc) {
            ext_desc->flags |= HTT_MSDU_CHECKSUM_ENABLE;
        }
    }
#endif // NEEDS PORTING

    /* Prevent firmware from sending up tx inspection requests. There's
     * nothing ath10k can do with frames requested for inspection so force
     * it to simply rely a regular tx completion with discard status.
     */
    flags1 |= HTT_DATA_TX_DESC_FLAGS1_POSTPONED;

    txbuf->cmd_hdr.msg_type = HTT_H2T_MSG_TYPE_TX_FRM;
    txbuf->cmd_tx.flags0 = flags0;
    txbuf->cmd_tx.flags1 = flags1;
    txbuf->cmd_tx.len = msdu->used;
    txbuf->cmd_tx.id = msdu_id;
    txbuf->cmd_tx.frags_paddr = frags_paddr;
    if (ath10k_mac_tx_frm_has_freq(ar)) {
        txbuf->cmd_tx.offchan_tx.peerid = HTT_INVALID_PEERID;
        txbuf->cmd_tx.offchan_tx.freq = freq;
    } else {
        txbuf->cmd_tx.peerid = HTT_INVALID_PEERID;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt tx flags0 %hhu flags1 %hu len %d id %hu frags_paddr %08x, msdu_paddr %08x vdev %hhu tid %hhu freq %hu\n",
               flags0, flags1, msdu->used, msdu_id, frags_paddr,
               (uint32_t)msdu->paddr, vdev_id, tid, freq);
    ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt tx msdu: ",
                    ath10k_msg_buf_get_payload(msdu), msdu->used);

    sg_items[0].transfer_id = 0;
    sg_items[0].transfer_context = NULL;
    sg_items[0].vaddr = &txbuf->htc_hdr;
    sg_items[0].paddr = txbuf_paddr +
                        sizeof(txbuf->frags);
    sg_items[0].len = sizeof(txbuf->htc_hdr) +
                      sizeof(txbuf->cmd_hdr) +
                      sizeof(txbuf->cmd_tx);

    sg_items[1].transfer_id = 0;
    sg_items[1].transfer_context = NULL;
    sg_items[1].vaddr = ath10k_msg_buf_get_payload(msdu);
    sg_items[1].paddr = msdu->paddr;
    sg_items[1].len = prefetch_len;

    ret = ath10k_hif_tx_sg(htt->ar,
                           htt->ar->htc.endpoint[htt->eid].ul_pipe_id,
                           sg_items, countof(sg_items));
    if (ret != ZX_OK) {
        ath10k_warn("failed to transmit msdu %d\n", msdu_id);
        goto err_free_msdu_id;
    }

    return ZX_OK;

err_free_msdu_id:
    ath10k_htt_tx_free_msdu_id(htt, msdu_id);
err:
    return ret;
}
