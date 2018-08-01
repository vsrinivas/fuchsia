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

#include "txrx.h"

#include "core.h"
#include "debug.h"
#include "htt.h"
#include "mac.h"

#if 0 // NEEDS PORTING
static void ath10k_report_offchan_tx(struct ath10k* ar, struct sk_buff* skb) {
    struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);

    if (likely(!(info->flags & IEEE80211_TX_CTL_TX_OFFCHAN))) {
        return;
    }

    if (ath10k_mac_tx_frm_has_freq(ar)) {
        return;
    }

    /* If the original wait_for_completion() timed out before
     * {data,mgmt}_tx_completed() was called then we could complete
     * offchan_tx_completed for a different skb. Prevent this by using
     * offchan_tx_skb.
     */
    mtx_lock(&ar->data_lock);
    if (ar->offchan_tx_skb != skb) {
        ath10k_warn("completed old offchannel frame\n");
        goto out;
    }

    complete(&ar->offchan_tx_completed);
    ar->offchan_tx_skb = NULL; /* just for sanity */

    ath10k_dbg(ar, ATH10K_DBG_HTT, "completed offchannel skb %pK\n", skb);
out:
    mtx_unlock(&ar->data_lock);
}
#endif // NEEDS PORTING

zx_status_t ath10k_txrx_tx_unref(struct ath10k_htt* htt, const struct htt_tx_done* tx_done) {
    struct ath10k* ar = htt->ar;
    struct ath10k_msg_buf* msdu;

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt tx completion msdu_id %u status %d\n",
               tx_done->msdu_id, tx_done->status);

    if (tx_done->msdu_id >= htt->max_num_pending_tx) {
        ath10k_warn("warning: msdu_id %d too big, ignoring\n",
                    tx_done->msdu_id);
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&htt->tx_lock);
    msdu = sa_get(htt->pending_tx, tx_done->msdu_id);
    if (msdu == NULL) {
        ath10k_warn("received tx completion for invalid msdu_id: %d\n",
                    tx_done->msdu_id);
        mtx_unlock(&htt->tx_lock);
        return ZX_ERR_IO;
    }

    ath10k_htt_tx_free_msdu_id(htt, tx_done->msdu_id);
    ath10k_htt_tx_dec_pending(htt);
    mtx_unlock(&htt->tx_lock);

#if 0 // NEEDS PORTING
    dma_unmap_single(dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);

    ath10k_report_offchan_tx(htt->ar, msdu);

    info = IEEE80211_SKB_CB(msdu);
    memset(&info->status, 0, sizeof(info->status));

    if (tx_done->status == HTT_TX_COMPL_STATE_DISCARD) {
        ieee80211_free_txskb(htt->ar->hw, msdu);
        return 0;
    }

    if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
        info->flags |= IEEE80211_TX_STAT_ACK;
    }

    if (tx_done->status == HTT_TX_COMPL_STATE_NOACK) {
        info->flags &= ~IEEE80211_TX_STAT_ACK;
    }

    if ((tx_done->status == HTT_TX_COMPL_STATE_ACK) &&
            (info->flags & IEEE80211_TX_CTL_NO_ACK)) {
        info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
    }
#endif // NEEDS PORTING

    ath10k_msg_buf_free(msdu);
    return ZX_OK;
}

#if 0 // NEEDS PORTING
struct ath10k_peer* ath10k_peer_find(struct ath10k* ar, int vdev_id,
                                     const uint8_t* addr) {
    struct ath10k_peer* peer;

    ASSERT_MTX_HELD(&ar->data_lock);

    list_for_each_entry(peer, &ar->peers, list) {
        if (peer->vdev_id != vdev_id) {
            continue;
        }
        if (!ether_addr_equal(peer->addr, addr)) {
            continue;
        }

        return peer;
    }

    return NULL;
}

struct ath10k_peer* ath10k_peer_find_by_id(struct ath10k* ar, int peer_id) {
    struct ath10k_peer* peer;

    ASSERT_MTX_HELD(&ar->data_lock);

    list_for_each_entry(peer, &ar->peers, list)
    if (BITARR_TEST(peer->peer_ids, peer_id)) {
        return peer;
    }

    return NULL;
}

static int ath10k_wait_for_peer_common(struct ath10k* ar, int vdev_id,
                                       const uint8_t* addr, bool expect_mapped) {
    long time_left;

    time_left = wait_event_timeout(ar->peer_mapping_wq, ({
        bool mapped;

        mtx_lock(&ar->data_lock);
        mapped = !!ath10k_peer_find(ar, vdev_id, addr);
        mtx_unlock(&ar->data_lock);

        (mapped == expect_mapped ||
         BITARR_TEST(&ar->dev_flags, ATH10K_FLAG_CRASH_FLUSH));
    }), 3 * HZ);

    if (time_left == 0) {
        return -ETIMEDOUT;
    }

    return 0;
}

int ath10k_wait_for_peer_created(struct ath10k* ar, int vdev_id, const uint8_t* addr) {
    return ath10k_wait_for_peer_common(ar, vdev_id, addr, true);
}

int ath10k_wait_for_peer_deleted(struct ath10k* ar, int vdev_id, const uint8_t* addr) {
    return ath10k_wait_for_peer_common(ar, vdev_id, addr, false);
}

void ath10k_peer_map_event(struct ath10k_htt* htt,
                           struct htt_peer_map_event* ev) {
    struct ath10k* ar = htt->ar;
    struct ath10k_peer* peer;

    if (ev->peer_id >= ATH10K_MAX_NUM_PEER_IDS) {
        ath10k_warn("received htt peer map event with idx out of bounds: %hu\n",
                    ev->peer_id);
        return;
    }

    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find(ar, ev->vdev_id, ev->addr);
    if (!peer) {
        peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
        if (!peer) {
            goto exit;
        }

        peer->vdev_id = ev->vdev_id;
        memcpy(peer->addr, ev->addr, ETH_ALEN);
        list_add(&peer->list, &ar->peers);
        wake_up(&ar->peer_mapping_wq);
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt peer map vdev %d peer %pM id %d\n",
               ev->vdev_id, ev->addr, ev->peer_id);

    COND_WARN(ar->peer_map[ev->peer_id] && (ar->peer_map[ev->peer_id] != peer));
    ar->peer_map[ev->peer_id] = peer;
    BITARR_SET(peer->peer_ids, ev->peer_id);
exit:
    mtx_unlock(&ar->data_lock);
}

void ath10k_peer_unmap_event(struct ath10k_htt* htt,
                             struct htt_peer_unmap_event* ev) {
    struct ath10k* ar = htt->ar;
    struct ath10k_peer* peer;

    if (ev->peer_id >= ATH10K_MAX_NUM_PEER_IDS) {
        ath10k_warn("received htt peer unmap event with idx out of bounds: %hu\n",
                    ev->peer_id);
        return;
    }

    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find_by_id(ar, ev->peer_id);
    if (!peer) {
        ath10k_warn("peer-unmap-event: unknown peer id %d\n",
                    ev->peer_id);
        goto exit;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt peer unmap vdev %d peer %pM id %d\n",
               peer->vdev_id, peer->addr, ev->peer_id);

    ar->peer_map[ev->peer_id] = NULL;
    BITARR_CLEAR(peer->peer_ids, ev->peer_id);

    if (bitmap_empty(peer->peer_ids, ATH10K_MAX_NUM_PEER_IDS)) {
        list_del(&peer->list);
        kfree(peer);
        wake_up(&ar->peer_mapping_wq);
    }

exit:
    mtx_unlock(&ar->data_lock);
}
#endif // NEEDS PORTING
