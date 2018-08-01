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

#include <assert.h>
#include <string.h>

#include <ddk/driver.h>
#include <hw/arch_ops.h>
#include <zircon/misc/fnv1hash.h>

#include "core.h"
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "htt.h"
#include "mac.h"
#include "macros.h"

#define HTT_RX_RING_SIZE HTT_RX_RING_SIZE_MAX
#define HTT_RX_RING_FILL_LEVEL (((HTT_RX_RING_SIZE) / 2) - 1)

/* when under memory pressure rx ring refill may fail and needs a retry */
#define HTT_RX_RING_REFILL_RETRY_MS 50

#define HTT_RX_RING_REFILL_RESCHED_MS 5

#if 0 // NEEDS PORTING
static int ath10k_htt_rx_get_csum_state(struct sk_buff* skb);
#endif // NEEDS PORTING

static_assert(IS_POW2(HTT_RX_BUF_HTABLE_SZ),
              "Invalid hash table size, must be power of 2");

static struct ath10k_msg_buf*
ath10k_htt_rx_find_msg_buf_paddr(struct ath10k* ar, uint32_t paddr) {
    uint32_t hash = fnv1a_tiny(paddr, ROUNDUP_LOG2(HTT_RX_BUF_HTABLE_SZ));
    ZX_DEBUG_ASSERT(hash < HTT_RX_BUF_HTABLE_SZ);
    list_node_t* candidate_list = &ar->htt.rx_ring.buf_hash[hash];
    struct ath10k_msg_buf* entry;
    struct ath10k_msg_buf* temp_entry;
    list_for_every_entry_safe(candidate_list, entry, temp_entry, struct ath10k_msg_buf, listnode) {
        if (entry->paddr == paddr) {
            return entry;
        }
    }

    ath10k_warn("Unable to find buffer corresponding to phys addr %x\n", paddr);
    return NULL;
}

static void ath10k_htt_rx_ring_free(struct ath10k_htt* htt) {
    struct ath10k_msg_buf* buf;
    int i;

    if (htt->rx_ring.in_ord_rx == ATH10K_HTT_IN_ORD_RX_YES) {
        for (i = 0; i < HTT_RX_BUF_HTABLE_SZ; i++) {
            list_node_t* list = &htt->rx_ring.buf_hash[i];
            while((buf = list_remove_head_type(list, struct ath10k_msg_buf, listnode)) != NULL) {
                ath10k_msg_buf_free(buf);
            }
        }
    } else {
        for (i = 0; i < htt->rx_ring.size; i++) {
            buf = htt->rx_ring.netbufs_ring[i];
            if (buf == NULL) {
                continue;
            }
            ath10k_msg_buf_free(buf);
        }
    }

    htt->rx_ring.fill_cnt = 0;
    memset(htt->rx_ring.netbufs_ring, 0,
           htt->rx_ring.size * sizeof(htt->rx_ring.netbufs_ring[0]));
}

static zx_status_t __ath10k_htt_rx_ring_fill_n(struct ath10k_htt* htt, int num) {
    struct htt_rx_desc* rx_desc;
    struct ath10k_msg_buf* buf;
    zx_status_t ret = ZX_OK;
    int idx;

    /* The Full Rx Reorder firmware has no way of telling the host
     * implicitly when it copied HTT Rx Ring buffers to MAC Rx Ring.
     * To keep things simple make sure ring is always half empty. This
     * guarantees there'll be no replenishment overruns possible.
     */
    static_assert(HTT_RX_RING_FILL_LEVEL < HTT_RX_RING_SIZE / 2,
                  "Ring fill must be less than half the total ring size");

    idx = *htt->rx_ring.alloc_idx.vaddr;
    while (num > 0) {
        ret = ath10k_msg_buf_alloc(htt->ar, &buf, ATH10K_MSG_TYPE_BASE, HTT_RX_BUF_SIZE);
        if (ret != ZX_OK) {
            goto fail;
        }

        ZX_DEBUG_ASSERT(IS_ALIGNED(buf->vaddr, HTT_RX_DESC_ALIGN));
        ZX_DEBUG_ASSERT((uintptr_t)buf->paddr + HTT_RX_BUF_SIZE <= 0x100000000);

        /* Clear rx_desc attention word before posting to Rx ring */
        rx_desc = buf->vaddr;
        rx_desc->attention.flags = 0;

        htt->rx_ring.netbufs_ring[idx] = buf;
        htt->rx_ring.paddrs_ring[idx] = buf->paddr;
        htt->rx_ring.fill_cnt++;

        if (htt->rx_ring.in_ord_rx == ATH10K_HTT_IN_ORD_RX_YES) {
            uint32_t hash = fnv1a_tiny(buf->paddr, ROUNDUP_LOG2(HTT_RX_BUF_HTABLE_SZ));
            ZX_DEBUG_ASSERT(hash < HTT_RX_BUF_HTABLE_SZ);
            list_node_t* bucket = &htt->rx_ring.buf_hash[hash];
            list_add_tail(bucket, &buf->listnode);
        }

        num--;
        idx++;
        idx &= htt->rx_ring.size_mask;
    }

fail:
    /*
     * Make sure the rx buffer is updated before available buffer
     * index to avoid any potential rx ring corruption.
     */
    hw_mb();
    *htt->rx_ring.alloc_idx.vaddr = idx;
    return ret;
}

static zx_status_t ath10k_htt_rx_ring_fill_n(struct ath10k_htt* htt, int num) {
    ASSERT_MTX_HELD(&htt->rx_ring.lock);
    return __ath10k_htt_rx_ring_fill_n(htt, num);
}

static void ath10k_htt_rx_msdu_buff_replenish(struct ath10k_htt* htt) {
    int num_deficit;

    /* Refilling the whole RX ring buffer proves to be a bad idea. The
     * reason is RX may take up significant amount of CPU cycles and starve
     * other tasks, e.g. TX on an ethernet device while acting as a bridge
     * with ath10k wlan interface. This ended up with very poor performance
     * once CPU the host system was overwhelmed with RX on ath10k.
     *
     * By limiting the number of refills the replenishing occurs
     * progressively. This in turns makes use of the fact tasklets are
     * processed in FIFO order. This means actual RX processing can starve
     * out refilling. If there's not enough buffers on RX ring FW will not
     * report RX until it is refilled with enough buffers. This
     * automatically balances load wrt to CPU power.
     *
     * This probably comes at a cost of lower maximum throughput but
     * improves the average and stability.
     */
    mtx_lock(&htt->rx_ring.lock);
    num_deficit = htt->rx_ring.fill_level - htt->rx_ring.fill_cnt;
    ath10k_htt_rx_ring_fill_n(htt, num_deficit);
    mtx_unlock(&htt->rx_ring.lock);
}

zx_status_t ath10k_htt_rx_ring_refill(struct ath10k* ar) {
    struct ath10k_htt* htt = &ar->htt;
    zx_status_t ret;

    mtx_lock(&htt->rx_ring.lock);
    ret = ath10k_htt_rx_ring_fill_n(htt, (htt->rx_ring.fill_level -
                                          htt->rx_ring.fill_cnt));
    mtx_unlock(&htt->rx_ring.lock);

    if (ret != ZX_OK) {
        ath10k_htt_rx_ring_free(htt);
    }

    return ret;
}

void ath10k_htt_rx_free(struct ath10k_htt* htt) {
    ath10k_htt_rx_ring_free(htt);
    io_buffer_release(&htt->rx_ring.io_buf);
    io_buffer_release(&htt->rx_ring.alloc_idx.io_buf);
    free(htt->rx_ring.netbufs_ring);
}

#if 0 // NEEDS PORTING
static inline struct sk_buff* ath10k_htt_rx_netbuf_pop(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    int idx;
    struct sk_buff* msdu;

    ASSERT_MTX_HELD(&htt->rx_ring.lock);

    if (htt->rx_ring.fill_cnt == 0) {
        ath10k_warn("tried to pop sk_buff from an empty rx ring\n");
        return NULL;
    }

    idx = htt->rx_ring.sw_rd_idx.msdu_payld;
    msdu = htt->rx_ring.netbufs_ring[idx];
    htt->rx_ring.netbufs_ring[idx] = NULL;
    htt->rx_ring.paddrs_ring[idx] = 0;

    idx++;
    idx &= htt->rx_ring.size_mask;
    htt->rx_ring.sw_rd_idx.msdu_payld = idx;
    htt->rx_ring.fill_cnt--;

    dma_unmap_single(htt->ar->dev,
                     ATH10K_SKB_RXCB(msdu)->paddr,
                     msdu->len + skb_tailroom(msdu),
                     DMA_FROM_DEVICE);
    ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx netbuf pop: ",
                    msdu->data, msdu->len + skb_tailroom(msdu));

    return msdu;
}

/* return: < 0 fatal error, 0 - non chained msdu, 1 chained msdu */
static int ath10k_htt_rx_amsdu_pop(struct ath10k_htt* htt,
                                   struct sk_buff_head* amsdu) {
    struct ath10k* ar = htt->ar;
    int msdu_len, msdu_chaining = 0;
    struct sk_buff* msdu;
    struct htt_rx_desc* rx_desc;

    ASSERT_MTX_HELD(&htt->rx_ring.lock);

    for (;;) {
        int last_msdu, msdu_len_invalid, msdu_chained;

        msdu = ath10k_htt_rx_netbuf_pop(htt);
        if (!msdu) {
            __skb_queue_purge(amsdu);
            return -ENOENT;
        }

        __skb_queue_tail(amsdu, msdu);

        rx_desc = (struct htt_rx_desc*)msdu->data;

        /* FIXME: we must report msdu payload since this is what caller
         * expects now
         */
        skb_put(msdu, offsetof(struct htt_rx_desc, msdu_payload));
        skb_pull(msdu, offsetof(struct htt_rx_desc, msdu_payload));

        /*
         * Sanity check - confirm the HW is finished filling in the
         * rx data.
         * If the HW and SW are working correctly, then it's guaranteed
         * that the HW's MAC DMA is done before this point in the SW.
         * To prevent the case that we handle a stale Rx descriptor,
         * just assert for now until we have a way to recover.
         */
        if (!(rx_desc->attention.flags
                & RX_ATTENTION_FLAGS_MSDU_DONE)) {
            __skb_queue_purge(amsdu);
            return -EIO;
        }

        msdu_len_invalid = !!(rx_desc->attention.flags
                              & (RX_ATTENTION_FLAGS_MPDU_LENGTH_ERR |
                                 RX_ATTENTION_FLAGS_MSDU_LENGTH_ERR));
        msdu_len = MS(rx_desc->msdu_start.common.info0,
                      RX_MSDU_START_INFO0_MSDU_LENGTH);
        msdu_chained = rx_desc->frag_info.ring2_more_count;

        if (msdu_len_invalid) {
            msdu_len = 0;
        }

        skb_trim(msdu, 0);
        skb_put(msdu, MIN(msdu_len, HTT_RX_MSDU_SIZE));
        msdu_len -= msdu->len;

        /* Note: Chained buffers do not contain rx descriptor */
        while (msdu_chained--) {
            msdu = ath10k_htt_rx_netbuf_pop(htt);
            if (!msdu) {
                __skb_queue_purge(amsdu);
                return -ENOENT;
            }

            __skb_queue_tail(amsdu, msdu);
            skb_trim(msdu, 0);
            skb_put(msdu, MIN(msdu_len, HTT_RX_BUF_SIZE));
            msdu_len -= msdu->len;
            msdu_chaining = 1;
        }

        last_msdu = rx_desc->msdu_end.common.info0 &
                    RX_MSDU_END_INFO0_LAST_MSDU;

        trace_ath10k_htt_rx_desc(ar, &rx_desc->attention,
                                 sizeof(*rx_desc) - sizeof(uint32_t));

        if (last_msdu) {
            break;
        }
    }

    if (skb_queue_empty(amsdu)) {
        msdu_chaining = -1;
    }

    /*
     * Don't refill the ring yet.
     *
     * First, the elements popped here are still in use - it is not
     * safe to overwrite them until the matching call to
     * mpdu_desc_list_next. Second, for efficiency it is preferable to
     * refill the rx ring with 1 PPDU's worth of rx buffers (something
     * like 32 x 3 buffers), rather than one MPDU's worth of rx buffers
     * (something like 3 buffers). Consequently, we'll rely on the txrx
     * SW to tell us when it is done pulling all the PPDU's rx buffers
     * out of the rx ring, and then refill it just once.
     */

    return msdu_chaining;
}
#endif // NEEDS PORTING

static struct ath10k_msg_buf* ath10k_htt_rx_pop_paddr(struct ath10k_htt* htt, uint32_t paddr) {
    struct ath10k* ar = htt->ar;
    struct ath10k_msg_buf* msdu;

    ASSERT_MTX_HELD(&htt->rx_ring.lock);

    msdu = ath10k_htt_rx_find_msg_buf_paddr(ar, paddr);
    if (!msdu) {
        return NULL;
    }

    list_delete(&msdu->listnode);
    htt->rx_ring.fill_cnt--;

#if 0 // NEEDS PORTING
    dma_unmap_single(htt->ar->dev, rxcb->paddr,
                     msdu->len + skb_tailroom(msdu),
                     DMA_FROM_DEVICE);
#endif // NEEDS PORTING

    ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx netbuf pop: ",
                    msdu->vaddr, msdu->used);

    return msdu;
}

#if 0 // NEEDS PORTING
static zx_status_t ath10k_htt_rx_pop_paddr_list(struct ath10k_htt* htt,
                                                struct htt_rx_in_ord_ind* ev,
                                                list_node_t* list) {
    struct htt_rx_in_ord_msdu_desc* msdu_desc = ev->msdu_descs;
    struct htt_rx_desc* rxd;
    struct ath10k_msg_buf* msdu;
    int msdu_count;
    bool is_offload;
    uint32_t paddr;

    ASSERT_MTX_HELD(&htt->rx_ring.lock);

    msdu_count = ev->msdu_count;
    is_offload = !!(ev->info & HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK);

    while (msdu_count--) {
        paddr = msdu_desc->msdu_paddr;

        msdu = ath10k_htt_rx_pop_paddr(htt, paddr);
        if (msdu == NULL) {
            __skb_queue_purge(list);
            return ZX_ERR_NOT_FOUND;
        }

        list_add_tail(list, &msdu->listnode);

        if (!is_offload) {
            msdu->type = ATH10K_MSG_TYPE_HTT_RX;
            rxd = ath10k_msg_buf_get_header(msdu, ATH10K_MSG_TYPE_HTT_RX);

            size_t extra = sizeof(*rxd) + msdu_desc->msdu_len;
            ZX_DEBUG_ASSERT((msdu->used + extra) <= msdu->capacity);
            msdu->used += extra;

            if (!(rxd->attention.flags &
                    RX_ATTENTION_FLAGS_MSDU_DONE)) {
                ath10k_warn("tried to pop an incomplete frame, oops!\n");
                return ZX_ERR_IO;
            }
        }

        msdu_desc++;
    }

    return ZX_OK;
}
#endif // NEEDS PORTING

zx_status_t ath10k_htt_rx_alloc(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    zx_status_t ret;

    htt->rx_confused = false;

    /* XXX: The fill level could be changed during runtime in response to
     * the host processing latency. Is this really worth it?
     */
    htt->rx_ring.size = HTT_RX_RING_SIZE;
    htt->rx_ring.size_mask = htt->rx_ring.size - 1;
    htt->rx_ring.fill_level = HTT_RX_RING_FILL_LEVEL;

    if (!IS_POW2(htt->rx_ring.size)) {
        ath10k_warn("htt rx ring size (%d) is not power of 2\n", htt->rx_ring.size);
        return ZX_ERR_INVALID_ARGS;
    }

    htt->rx_ring.netbufs_ring = calloc(1, htt->rx_ring.size * sizeof(struct ath10k_msg_buf*));
    if (htt->rx_ring.netbufs_ring == NULL) {
        goto err_netbuf;
    }

    size_t ring_size = htt->rx_ring.size * sizeof(htt->rx_ring.paddrs_ring);

    zx_handle_t bti_handle;
    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) {
        goto err_dma_ring;
    }

    // Can this be a IO_BUFFER_RO?
    ret = io_buffer_init(&htt->rx_ring.io_buf, bti_handle, ring_size,
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        goto err_dma_ring;
    }
    htt->rx_ring.paddrs_ring = io_buffer_virt(&htt->rx_ring.io_buf);
    htt->rx_ring.base_paddr = io_buffer_phys(&htt->rx_ring.io_buf);
    if (htt->rx_ring.base_paddr + ring_size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        goto err_dma_idx;
    }

    size_t idx_size = sizeof(*htt->rx_ring.alloc_idx.vaddr);
    ret = io_buffer_init(&htt->rx_ring.alloc_idx.io_buf, bti_handle, idx_size,
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        goto err_dma_idx;
    }
    htt->rx_ring.alloc_idx.vaddr = io_buffer_virt(&htt->rx_ring.alloc_idx.io_buf);
    htt->rx_ring.alloc_idx.paddr = io_buffer_phys(&htt->rx_ring.alloc_idx.io_buf);
    if (htt->rx_ring.alloc_idx.paddr + idx_size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        goto err_dma_idx_map;
    }

    htt->rx_ring.sw_rd_idx.msdu_payld = htt->rx_ring.size_mask;
    *htt->rx_ring.alloc_idx.vaddr = 0;

    mtx_init(&htt->rx_ring.lock, mtx_plain);

    htt->rx_ring.fill_cnt = 0;
    htt->rx_ring.sw_rd_idx.msdu_payld = 0;

    for (unsigned ndx = 0; ndx < HTT_RX_BUF_HTABLE_SZ; ndx++) {
        list_initialize(&htt->rx_ring.buf_hash[ndx]);
    }

    atomic_store(&htt->num_mpdus_ready, 0);

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "htt rx ring size %d fill_level %d\n",
               htt->rx_ring.size, htt->rx_ring.fill_level);
    return ZX_OK;

err_dma_idx_map:
    io_buffer_release(&htt->rx_ring.alloc_idx.io_buf);
err_dma_idx:
    io_buffer_release(&htt->rx_ring.io_buf);
err_dma_ring:
    free(htt->rx_ring.netbufs_ring);
err_netbuf:
    return ZX_ERR_NO_MEMORY;
}

#if 0 // NEEDS PORTING
static int ath10k_htt_rx_crypto_param_len(struct ath10k* ar,
        enum htt_rx_mpdu_encrypt_type type) {
    switch (type) {
    case HTT_RX_MPDU_ENCRYPT_NONE:
        return 0;
    case HTT_RX_MPDU_ENCRYPT_WEP40:
    case HTT_RX_MPDU_ENCRYPT_WEP104:
        return IEEE80211_WEP_IV_LEN;
    case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
    case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
        return IEEE80211_TKIP_IV_LEN;
    case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
        return IEEE80211_CCMP_HDR_LEN;
    case HTT_RX_MPDU_ENCRYPT_WEP128:
    case HTT_RX_MPDU_ENCRYPT_WAPI:
        break;
    }

    ath10k_warn("unsupported encryption type %d\n", type);
    return 0;
}

#define MICHAEL_MIC_LEN 8

static int ath10k_htt_rx_crypto_tail_len(struct ath10k* ar,
        enum htt_rx_mpdu_encrypt_type type) {
    switch (type) {
    case HTT_RX_MPDU_ENCRYPT_NONE:
        return 0;
    case HTT_RX_MPDU_ENCRYPT_WEP40:
    case HTT_RX_MPDU_ENCRYPT_WEP104:
        return IEEE80211_WEP_ICV_LEN;
    case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
    case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
        return IEEE80211_TKIP_ICV_LEN;
    case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
        return IEEE80211_CCMP_MIC_LEN;
    case HTT_RX_MPDU_ENCRYPT_WEP128:
    case HTT_RX_MPDU_ENCRYPT_WAPI:
        break;
    }

    ath10k_warn("unsupported encryption type %d\n", type);
    return 0;
}

struct amsdu_subframe_hdr {
    uint8_t dst[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    __be16 len;
} __PACKED;

#define GROUP_ID_IS_SU_MIMO(x) ((x) == 0 || (x) == 63)

static void ath10k_htt_rx_h_rates(struct ath10k* ar,
                                  struct ieee80211_rx_status* status,
                                  struct htt_rx_desc* rxd) {
    struct ieee80211_supported_band* sband;
    uint8_t cck, rate, bw, sgi, mcs, nss;
    uint8_t preamble = 0;
    uint8_t group_id;
    uint32_t info1, info2, info3;

    info1 = rxd->ppdu_start.info1;
    info2 = rxd->ppdu_start.info2;
    info3 = rxd->ppdu_start.info3;

    preamble = MS(info1, RX_PPDU_START_INFO1_PREAMBLE_TYPE);

    switch (preamble) {
    case HTT_RX_LEGACY:
        /* To get legacy rate index band is required. Since band can't
         * be undefined check if freq is non-zero.
         */
        if (!status->freq) {
            return;
        }

        cck = info1 & RX_PPDU_START_INFO1_L_SIG_RATE_SELECT;
        rate = MS(info1, RX_PPDU_START_INFO1_L_SIG_RATE);
        rate &= ~RX_PPDU_START_RATE_FLAG;

        sband = &ar->mac.sbands[status->band];
        status->rate_idx = ath10k_mac_hw_rate_to_idx(sband, rate, cck);
        break;
    case HTT_RX_HT:
    case HTT_RX_HT_WITH_TXBF:
        /* HT-SIG - Table 20-11 in info2 and info3 */
        mcs = info2 & 0x1F;
        nss = mcs >> 3;
        bw = (info2 >> 7) & 1;
        sgi = (info3 >> 7) & 1;

        status->rate_idx = mcs;
        status->encoding = RX_ENC_HT;
        if (sgi) {
            status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
        }
        if (bw) {
            status->bw = RATE_INFO_BW_40;
        }
        break;
    case HTT_RX_VHT:
    case HTT_RX_VHT_WITH_TXBF:
        /* VHT-SIG-A1 in info2, VHT-SIG-A2 in info3
         * TODO check this
         */
        bw = info2 & 3;
        sgi = info3 & 1;
        group_id = (info2 >> 4) & 0x3F;

        if (GROUP_ID_IS_SU_MIMO(group_id)) {
            mcs = (info3 >> 4) & 0x0F;
            nss = ((info2 >> 10) & 0x07) + 1;
        } else {
            /* Hardware doesn't decode VHT-SIG-B into Rx descriptor
             * so it's impossible to decode MCS. Also since
             * firmware consumes Group Id Management frames host
             * has no knowledge regarding group/user position
             * mapping so it's impossible to pick the correct Nsts
             * from VHT-SIG-A1.
             *
             * Bandwidth and SGI are valid so report the rateinfo
             * on best-effort basis.
             */
            mcs = 0;
            nss = 1;
        }

        if (mcs > 0x09) {
            ath10k_warn("invalid MCS received %u\n", mcs);
            ath10k_warn("rxd %08x mpdu start %08x %08x msdu start %08x %08x ppdu start %08x %08x %08x %08x %08x\n",
                        rxd->attention.flags,
                        rxd->mpdu_start.info0,
                        rxd->mpdu_start.info1,
                        rxd->msdu_start.common.info0,
                        rxd->msdu_start.common.info1,
                        rxd->ppdu_start.info0,
                        rxd->ppdu_start.info1,
                        rxd->ppdu_start.info2,
                        rxd->ppdu_start.info3,
                        rxd->ppdu_start.info4);

            ath10k_warn("msdu end %08x mpdu end %08x\n",
                        rxd->msdu_end.common.info0,
                        rxd->mpdu_end.info0);

            ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL,
                            "rx desc msdu payload: ",
                            rxd->msdu_payload, 50);
        }

        status->rate_idx = mcs;
        status->nss = nss;

        if (sgi) {
            status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
        }

        switch (bw) {
        /* 20MHZ */
        case 0:
            break;
        /* 40MHZ */
        case 1:
            status->bw = RATE_INFO_BW_40;
            break;
        /* 80MHZ */
        case 2:
            status->bw = RATE_INFO_BW_80;
            break;
        case 3:
            status->bw = RATE_INFO_BW_160;
            break;
        }

        status->encoding = RX_ENC_VHT;
        break;
    default:
        break;
    }
}

static struct ieee80211_channel*
ath10k_htt_rx_h_peer_channel(struct ath10k* ar, struct htt_rx_desc* rxd) {
    struct ath10k_peer* peer;
    struct ath10k_vif* arvif;
    struct cfg80211_chan_def def;
    uint16_t peer_id;

    ASSERT_MTX_HELD(&ar->data_lock);

    if (!rxd) {
        return NULL;
    }

    if (rxd->attention.flags&
            RX_ATTENTION_FLAGS_PEER_IDX_INVALID) {
        return NULL;
    }

    if (!(rxd->msdu_end.common.info0&
            RX_MSDU_END_INFO0_FIRST_MSDU)) {
        return NULL;
    }

    peer_id = MS(rxd->mpdu_start.info0,
                 RX_MPDU_START_INFO0_PEER_IDX);

    peer = ath10k_peer_find_by_id(ar, peer_id);
    if (!peer) {
        return NULL;
    }

    arvif = ath10k_get_arvif(ar, peer->vdev_id);
    if (COND_WARN_ONCE(!arvif)) {
        return NULL;
    }

    if (ath10k_mac_vif_chan(arvif->vif, &def)) {
        return NULL;
    }

    return def.chan;
}

static struct ieee80211_channel*
ath10k_htt_rx_h_vdev_channel(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_vif* arvif;
    struct cfg80211_chan_def def;

    ASSERT_MTX_HELD(&ar->data_lock);

    list_for_each_entry(arvif, &ar->arvifs, list) {
        if (arvif->vdev_id == vdev_id &&
                ath10k_mac_vif_chan(arvif->vif, &def) == 0) {
            return def.chan;
        }
    }

    return NULL;
}

static void
ath10k_htt_rx_h_any_chan_iter(struct ieee80211_hw* hw,
                              struct ieee80211_chanctx_conf* conf,
                              void* data) {
    struct cfg80211_chan_def* def = data;

    *def = conf->def;
}

static struct ieee80211_channel*
ath10k_htt_rx_h_any_channel(struct ath10k* ar) {
    struct cfg80211_chan_def def = {};

    ieee80211_iter_chan_contexts_atomic(ar->hw,
                                        ath10k_htt_rx_h_any_chan_iter,
                                        &def);

    return def.chan;
}

static bool ath10k_htt_rx_h_channel(struct ath10k* ar,
                                    struct ieee80211_rx_status* status,
                                    struct htt_rx_desc* rxd,
                                    uint32_t vdev_id) {
    struct ieee80211_channel* ch;

    mtx_lock(&ar->data_lock);
    ch = ar->scan_channel;
    if (!ch) {
        ch = ar->rx_channel;
    }
    if (!ch) {
        ch = ath10k_htt_rx_h_peer_channel(ar, rxd);
    }
    if (!ch) {
        ch = ath10k_htt_rx_h_vdev_channel(ar, vdev_id);
    }
    if (!ch) {
        ch = ath10k_htt_rx_h_any_channel(ar);
    }
    if (!ch) {
        ch = ar->tgt_oper_chan;
    }
    mtx_unlock(&ar->data_lock);

    if (!ch) {
        return false;
    }

    status->band = ch->band;
    status->freq = ch->center_freq;

    return true;
}

static void ath10k_htt_rx_h_signal(struct ath10k* ar,
                                   struct ieee80211_rx_status* status,
                                   struct htt_rx_desc* rxd) {
    int i;

    for (i = 0; i < IEEE80211_MAX_CHAINS ; i++) {
        status->chains &= ~BIT(i);

        if (rxd->ppdu_start.rssi_chains[i].pri20_mhz != 0x80) {
            status->chain_signal[i] = ATH10K_DEFAULT_NOISE_FLOOR +
                                      rxd->ppdu_start.rssi_chains[i].pri20_mhz;

            status->chains |= BIT(i);
        }
    }

    /* FIXME: Get real NF */
    status->signal = ATH10K_DEFAULT_NOISE_FLOOR +
                     rxd->ppdu_start.rssi_comb;
    status->flag &= ~RX_FLAG_NO_SIGNAL_VAL;
}

static void ath10k_htt_rx_h_mactime(struct ath10k* ar,
                                    struct ieee80211_rx_status* status,
                                    struct htt_rx_desc* rxd) {
    /* FIXME: TSF is known only at the end of PPDU, in the last MPDU. This
     * means all prior MSDUs in a PPDU are reported to mac80211 without the
     * TSF. Is it worth holding frames until end of PPDU is known?
     *
     * FIXME: Can we get/compute 64bit TSF?
     */
    status->mactime = rxd->ppdu_end.common.tsf_timestamp;
    status->flag |= RX_FLAG_MACTIME_END;
}

static void ath10k_htt_rx_h_ppdu(struct ath10k* ar,
                                 struct sk_buff_head* amsdu,
                                 struct ieee80211_rx_status* status,
                                 uint32_t vdev_id) {
    struct sk_buff* first;
    struct htt_rx_desc* rxd;
    bool is_first_ppdu;
    bool is_last_ppdu;

    if (skb_queue_empty(amsdu)) {
        return;
    }

    first = skb_peek(amsdu);
    rxd = (void*)first->data - sizeof(*rxd);

    is_first_ppdu = !!(rxd->attention.flags &
                       RX_ATTENTION_FLAGS_FIRST_MPDU);
    is_last_ppdu = !!(rxd->attention.flags &
                      RX_ATTENTION_FLAGS_LAST_MPDU);

    if (is_first_ppdu) {
        /* New PPDU starts so clear out the old per-PPDU status. */
        status->freq = 0;
        status->rate_idx = 0;
        status->nss = 0;
        status->encoding = RX_ENC_LEGACY;
        status->bw = RATE_INFO_BW_20;
        status->flag &= ~RX_FLAG_MACTIME_END;
        status->flag |= RX_FLAG_NO_SIGNAL_VAL;

        ath10k_htt_rx_h_signal(ar, status, rxd);
        ath10k_htt_rx_h_channel(ar, status, rxd, vdev_id);
        ath10k_htt_rx_h_rates(ar, status, rxd);
    }

    if (is_last_ppdu) {
        ath10k_htt_rx_h_mactime(ar, status, rxd);
    }
}

static const char* const tid_to_ac[] = {
    "BE",
    "BK",
    "BK",
    "BE",
    "VI",
    "VI",
    "VO",
    "VO",
};

static char* ath10k_get_tid(struct ieee80211_hdr* hdr, char* out, size_t size) {
    uint8_t* qc;
    int tid;

    if (!ieee80211_is_data_qos(hdr->frame_control)) {
        return "";
    }

    qc = ieee80211_get_qos_ctl(hdr);
    tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
    if (tid < 8) {
        snprintf(out, size, "tid %d (%s)", tid, tid_to_ac[tid]);
    } else {
        snprintf(out, size, "tid %d", tid);
    }

    return out;
}

static void ath10k_process_rx(struct ath10k* ar,
                              struct ieee80211_rx_status* rx_status,
                              struct sk_buff* skb) {
    struct ieee80211_rx_status* status;
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;
    char tid[32];

    status = IEEE80211_SKB_RXCB(skb);
    *status = *rx_status;

    ath10k_dbg(ar, ATH10K_DBG_DATA,
               "rx skb %pK len %u peer %pM %s %s sn %u %s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
               skb,
               skb->len,
               ieee80211_get_SA(hdr),
               ath10k_get_tid(hdr, tid, sizeof(tid)),
               is_multicast_ether_addr(ieee80211_get_DA(hdr)) ?
               "mcast" : "ucast",
               (hdr->seq_ctrl & IEEE80211_SCTL_SEQ) >> 4,
               (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
               (status->encoding == RX_ENC_HT) ? "ht" : "",
               (status->encoding == RX_ENC_VHT) ? "vht" : "",
               (status->bw == RATE_INFO_BW_40) ? "40" : "",
               (status->bw == RATE_INFO_BW_80) ? "80" : "",
               (status->bw == RATE_INFO_BW_160) ? "160" : "",
               status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
               status->rate_idx,
               status->nss,
               status->freq,
               status->band, status->flag,
               !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
               !!(status->flag & RX_FLAG_MMIC_ERROR),
               !!(status->flag & RX_FLAG_AMSDU_MORE));
    ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "rx skb: ",
                    skb->data, skb->len);
    trace_ath10k_rx_hdr(ar, skb->data, skb->len);
    trace_ath10k_rx_payload(ar, skb->data, skb->len);

    ieee80211_rx_napi(ar->hw, NULL, skb, &ar->napi);
}

static int ath10k_htt_rx_nwifi_hdrlen(struct ath10k* ar,
                                      struct ieee80211_hdr* hdr) {
    int len = ieee80211_hdrlen(hdr->frame_control);

    if (!test_bit(ATH10K_FW_FEATURE_NO_NWIFI_DECAP_4ADDR_PADDING,
                  ar->running_fw->fw_file.fw_features)) {
        len = ROUNDUP(len, 4);
    }

    return len;
}

static void ath10k_htt_rx_h_undecap_raw(struct ath10k* ar,
                                        struct sk_buff* msdu,
                                        struct ieee80211_rx_status* status,
                                        enum htt_rx_mpdu_encrypt_type enctype,
                                        bool is_decrypted) {
    struct ieee80211_hdr* hdr;
    struct htt_rx_desc* rxd;
    size_t hdr_len;
    size_t crypto_len;
    bool is_first;
    bool is_last;

    rxd = (void*)msdu->data - sizeof(*rxd);
    is_first = !!(rxd->msdu_end.common.info0 &
                  RX_MSDU_END_INFO0_FIRST_MSDU);
    is_last = !!(rxd->msdu_end.common.info0 &
                 RX_MSDU_END_INFO0_LAST_MSDU);

    /* Delivered decapped frame:
     * [802.11 header]
     * [crypto param] <-- can be trimmed if !fcs_err &&
     *                    !decrypt_err && !peer_idx_invalid
     * [amsdu header] <-- only if A-MSDU
     * [rfc1042/llc]
     * [payload]
     * [FCS] <-- at end, needs to be trimmed
     */

    /* This probably shouldn't happen but warn just in case */
    if (unlikely(COND_WARN_ONCE(!is_first))) {
        return;
    }

    /* This probably shouldn't happen but warn just in case */
    if (unlikely(COND_WARN_ONCE(!(is_first && is_last)))) {
        return;
    }

    skb_trim(msdu, msdu->len - FCS_LEN);

    /* In most cases this will be true for sniffed frames. It makes sense
     * to deliver them as-is without stripping the crypto param. This is
     * necessary for software based decryption.
     *
     * If there's no error then the frame is decrypted. At least that is
     * the case for frames that come in via fragmented rx indication.
     */
    if (!is_decrypted) {
        return;
    }

    /* The payload is decrypted so strip crypto params. Start from tail
     * since hdr is used to compute some stuff.
     */

    hdr = (void*)msdu->data;

    /* Tail */
    if (status->flag & RX_FLAG_IV_STRIPPED)
        skb_trim(msdu, msdu->len -
                 ath10k_htt_rx_crypto_tail_len(ar, enctype));

    /* MMIC */
    if ((status->flag & RX_FLAG_MMIC_STRIPPED) &&
            !ieee80211_has_morefrags(hdr->frame_control) &&
            enctype == HTT_RX_MPDU_ENCRYPT_TKIP_WPA) {
        skb_trim(msdu, msdu->len - 8);
    }

    /* Head */
    if (status->flag & RX_FLAG_IV_STRIPPED) {
        hdr_len = ieee80211_hdrlen(hdr->frame_control);
        crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

        memmove((void*)msdu->data + crypto_len,
                (void*)msdu->data, hdr_len);
        skb_pull(msdu, crypto_len);
    }
}

static void ath10k_htt_rx_h_undecap_nwifi(struct ath10k* ar,
        struct sk_buff* msdu,
        struct ieee80211_rx_status* status,
        const uint8_t first_hdr[64]) {
    struct ieee80211_hdr* hdr;
    struct htt_rx_desc* rxd;
    size_t hdr_len;
    uint8_t da[ETH_ALEN];
    uint8_t sa[ETH_ALEN];
    int l3_pad_bytes;

    /* Delivered decapped frame:
     * [nwifi 802.11 header] <-- replaced with 802.11 hdr
     * [rfc1042/llc]
     *
     * Note: The nwifi header doesn't have QoS Control and is
     * (always?) a 3addr frame.
     *
     * Note2: There's no A-MSDU subframe header. Even if it's part
     * of an A-MSDU.
     */

    /* pull decapped header and copy SA & DA */
    rxd = (void*)msdu->data - sizeof(*rxd);

    l3_pad_bytes = ath10k_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);
    skb_put(msdu, l3_pad_bytes);

    hdr = (struct ieee80211_hdr*)(msdu->data + l3_pad_bytes);

    hdr_len = ath10k_htt_rx_nwifi_hdrlen(ar, hdr);
    memcpy(da, ieee80211_get_DA(hdr), ETH_ALEN);
    memcpy(sa, ieee80211_get_SA(hdr), ETH_ALEN);
    skb_pull(msdu, hdr_len);

    /* push original 802.11 header */
    hdr = (struct ieee80211_hdr*)first_hdr;
    hdr_len = ieee80211_hdrlen(hdr->frame_control);
    memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

    /* original 802.11 header has a different DA and in
     * case of 4addr it may also have different SA
     */
    hdr = (struct ieee80211_hdr*)msdu->data;
    memcpy(ieee80211_get_DA(hdr), da, ETH_ALEN);
    memcpy(ieee80211_get_SA(hdr), sa, ETH_ALEN);
}

static void* ath10k_htt_rx_h_find_rfc1042(struct ath10k* ar,
        struct sk_buff* msdu,
        enum htt_rx_mpdu_encrypt_type enctype) {
    struct ieee80211_hdr* hdr;
    struct htt_rx_desc* rxd;
    size_t hdr_len, crypto_len;
    void* rfc1042;
    bool is_first, is_last, is_amsdu;
    int bytes_aligned = ar->hw_params.decap_align_bytes;

    rxd = (void*)msdu->data - sizeof(*rxd);
    hdr = (void*)rxd->rx_hdr_status;

    is_first = !!(rxd->msdu_end.common.info0 &
                  RX_MSDU_END_INFO0_FIRST_MSDU);
    is_last = !!(rxd->msdu_end.common.info0 &
                 RX_MSDU_END_INFO0_LAST_MSDU);
    is_amsdu = !(is_first && is_last);

    rfc1042 = hdr;

    if (is_first) {
        hdr_len = ieee80211_hdrlen(hdr->frame_control);
        crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

        rfc1042 += ROUNDUP(hdr_len, bytes_aligned) +
                   ROUNDUP(crypto_len, bytes_aligned);
    }

    if (is_amsdu) {
        rfc1042 += sizeof(struct amsdu_subframe_hdr);
    }

    return rfc1042;
}

static void ath10k_htt_rx_h_undecap_eth(struct ath10k* ar,
                                        struct sk_buff* msdu,
                                        struct ieee80211_rx_status* status,
                                        const uint8_t first_hdr[64],
                                        enum htt_rx_mpdu_encrypt_type enctype) {
    struct ieee80211_hdr* hdr;
    struct ethhdr* eth;
    size_t hdr_len;
    void* rfc1042;
    uint8_t da[ETH_ALEN];
    uint8_t sa[ETH_ALEN];
    int l3_pad_bytes;
    struct htt_rx_desc* rxd;

    /* Delivered decapped frame:
     * [eth header] <-- replaced with 802.11 hdr & rfc1042/llc
     * [payload]
     */

    rfc1042 = ath10k_htt_rx_h_find_rfc1042(ar, msdu, enctype);
    if (COND_WARN_ONCE(!rfc1042)) {
        return;
    }

    rxd = (void*)msdu->data - sizeof(*rxd);
    l3_pad_bytes = ath10k_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);
    skb_put(msdu, l3_pad_bytes);
    skb_pull(msdu, l3_pad_bytes);

    /* pull decapped header and copy SA & DA */
    eth = (struct ethhdr*)msdu->data;
    memcpy(da, eth->h_dest, ETH_ALEN);
    memcpy(sa, eth->h_source, ETH_ALEN);
    skb_pull(msdu, sizeof(struct ethhdr));

    /* push rfc1042/llc/snap */
    memcpy(skb_push(msdu, sizeof(struct rfc1042_hdr)), rfc1042,
           sizeof(struct rfc1042_hdr));

    /* push original 802.11 header */
    hdr = (struct ieee80211_hdr*)first_hdr;
    hdr_len = ieee80211_hdrlen(hdr->frame_control);
    memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

    /* original 802.11 header has a different DA and in
     * case of 4addr it may also have different SA
     */
    hdr = (struct ieee80211_hdr*)msdu->data;
    memcpy(ieee80211_get_DA(hdr), da, ETH_ALEN);
    memcpy(ieee80211_get_SA(hdr), sa, ETH_ALEN);
}

static void ath10k_htt_rx_h_undecap_snap(struct ath10k* ar,
        struct sk_buff* msdu,
        struct ieee80211_rx_status* status,
        const uint8_t first_hdr[64]) {
    struct ieee80211_hdr* hdr;
    size_t hdr_len;
    int l3_pad_bytes;
    struct htt_rx_desc* rxd;

    /* Delivered decapped frame:
     * [amsdu header] <-- replaced with 802.11 hdr
     * [rfc1042/llc]
     * [payload]
     */

    rxd = (void*)msdu->data - sizeof(*rxd);
    l3_pad_bytes = ath10k_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);

    skb_put(msdu, l3_pad_bytes);
    skb_pull(msdu, sizeof(struct amsdu_subframe_hdr) + l3_pad_bytes);

    hdr = (struct ieee80211_hdr*)first_hdr;
    hdr_len = ieee80211_hdrlen(hdr->frame_control);
    memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);
}

static void ath10k_htt_rx_h_undecap(struct ath10k* ar,
                                    struct sk_buff* msdu,
                                    struct ieee80211_rx_status* status,
                                    uint8_t first_hdr[64],
                                    enum htt_rx_mpdu_encrypt_type enctype,
                                    bool is_decrypted) {
    struct htt_rx_desc* rxd;
    enum rx_msdu_decap_format decap;

    /* First msdu's decapped header:
     * [802.11 header] <-- padded to 4 bytes long
     * [crypto param] <-- padded to 4 bytes long
     * [amsdu header] <-- only if A-MSDU
     * [rfc1042/llc]
     *
     * Other (2nd, 3rd, ..) msdu's decapped header:
     * [amsdu header] <-- only if A-MSDU
     * [rfc1042/llc]
     */

    rxd = (void*)msdu->data - sizeof(*rxd);
    decap = MS(rxd->msdu_start.common.info1,
               RX_MSDU_START_INFO1_DECAP_FORMAT);

    switch (decap) {
    case RX_MSDU_DECAP_RAW:
        ath10k_htt_rx_h_undecap_raw(ar, msdu, status, enctype,
                                    is_decrypted);
        break;
    case RX_MSDU_DECAP_NATIVE_WIFI:
        ath10k_htt_rx_h_undecap_nwifi(ar, msdu, status, first_hdr);
        break;
    case RX_MSDU_DECAP_ETHERNET2_DIX:
        ath10k_htt_rx_h_undecap_eth(ar, msdu, status, first_hdr, enctype);
        break;
    case RX_MSDU_DECAP_8023_SNAP_LLC:
        ath10k_htt_rx_h_undecap_snap(ar, msdu, status, first_hdr);
        break;
    }
}

static int ath10k_htt_rx_get_csum_state(struct sk_buff* skb) {
    struct htt_rx_desc* rxd;
    uint32_t flags, info;
    bool is_ip4, is_ip6;
    bool is_tcp, is_udp;
    bool ip_csum_ok, tcpudp_csum_ok;

    rxd = (void*)skb->data - sizeof(*rxd);
    flags = rxd->attention.flags;
    info = rxd->msdu_start.common.info1;

    is_ip4 = !!(info & RX_MSDU_START_INFO1_IPV4_PROTO);
    is_ip6 = !!(info & RX_MSDU_START_INFO1_IPV6_PROTO);
    is_tcp = !!(info & RX_MSDU_START_INFO1_TCP_PROTO);
    is_udp = !!(info & RX_MSDU_START_INFO1_UDP_PROTO);
    ip_csum_ok = !(flags & RX_ATTENTION_FLAGS_IP_CHKSUM_FAIL);
    tcpudp_csum_ok = !(flags & RX_ATTENTION_FLAGS_TCP_UDP_CHKSUM_FAIL);

    if (!is_ip4 && !is_ip6) {
        return CHECKSUM_NONE;
    }
    if (!is_tcp && !is_udp) {
        return CHECKSUM_NONE;
    }
    if (!ip_csum_ok) {
        return CHECKSUM_NONE;
    }
    if (!tcpudp_csum_ok) {
        return CHECKSUM_NONE;
    }

    return CHECKSUM_UNNECESSARY;
}

static void ath10k_htt_rx_h_csum_offload(struct sk_buff* msdu) {
    msdu->ip_summed = ath10k_htt_rx_get_csum_state(msdu);
}

static void ath10k_htt_rx_h_mpdu(struct ath10k* ar,
                                 struct sk_buff_head* amsdu,
                                 struct ieee80211_rx_status* status) {
    struct sk_buff* first;
    struct sk_buff* last;
    struct sk_buff* msdu;
    struct htt_rx_desc* rxd;
    struct ieee80211_hdr* hdr;
    enum htt_rx_mpdu_encrypt_type enctype;
    uint8_t first_hdr[64];
    uint8_t* qos;
    size_t hdr_len;
    bool has_fcs_err;
    bool has_crypto_err;
    bool has_tkip_err;
    bool has_peer_idx_invalid;
    bool is_decrypted;
    bool is_mgmt;
    uint32_t attention;

    if (skb_queue_empty(amsdu)) {
        return;
    }

    first = skb_peek(amsdu);
    rxd = (void*)first->data - sizeof(*rxd);

    is_mgmt = !!(rxd->attention.flags &
                 RX_ATTENTION_FLAGS_MGMT_TYPE);

    enctype = MS(rxd->mpdu_start.info0,
                 RX_MPDU_START_INFO0_ENCRYPT_TYPE);

    /* First MSDU's Rx descriptor in an A-MSDU contains full 802.11
     * decapped header. It'll be used for undecapping of each MSDU.
     */
    hdr = (void*)rxd->rx_hdr_status;
    hdr_len = ieee80211_hdrlen(hdr->frame_control);
    memcpy(first_hdr, hdr, hdr_len);

    /* Each A-MSDU subframe will use the original header as the base and be
     * reported as a separate MSDU so strip the A-MSDU bit from QoS Ctl.
     */
    hdr = (void*)first_hdr;
    qos = ieee80211_get_qos_ctl(hdr);
    qos[0] &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;

    /* Some attention flags are valid only in the last MSDU. */
    last = skb_peek_tail(amsdu);
    rxd = (void*)last->data - sizeof(*rxd);
    attention = rxd->attention.flags;

    has_fcs_err = !!(attention & RX_ATTENTION_FLAGS_FCS_ERR);
    has_crypto_err = !!(attention & RX_ATTENTION_FLAGS_DECRYPT_ERR);
    has_tkip_err = !!(attention & RX_ATTENTION_FLAGS_TKIP_MIC_ERR);
    has_peer_idx_invalid = !!(attention & RX_ATTENTION_FLAGS_PEER_IDX_INVALID);

    /* Note: If hardware captures an encrypted frame that it can't decrypt,
     * e.g. due to fcs error, missing peer or invalid key data it will
     * report the frame as raw.
     */
    is_decrypted = (enctype != HTT_RX_MPDU_ENCRYPT_NONE &&
                    !has_fcs_err &&
                    !has_crypto_err &&
                    !has_peer_idx_invalid);

    /* Clear per-MPDU flags while leaving per-PPDU flags intact. */
    status->flag &= ~(RX_FLAG_FAILED_FCS_CRC |
                      RX_FLAG_MMIC_ERROR |
                      RX_FLAG_DECRYPTED |
                      RX_FLAG_IV_STRIPPED |
                      RX_FLAG_ONLY_MONITOR |
                      RX_FLAG_MMIC_STRIPPED);

    if (has_fcs_err) {
        status->flag |= RX_FLAG_FAILED_FCS_CRC;
    }

    if (has_tkip_err) {
        status->flag |= RX_FLAG_MMIC_ERROR;
    }

    /* Firmware reports all necessary management frames via WMI already.
     * They are not reported to monitor interfaces at all so pass the ones
     * coming via HTT to monitor interfaces instead. This simplifies
     * matters a lot.
     */
    if (is_mgmt) {
        status->flag |= RX_FLAG_ONLY_MONITOR;
    }

    if (is_decrypted) {
        status->flag |= RX_FLAG_DECRYPTED;

        if (likely(!is_mgmt))
            status->flag |= RX_FLAG_IV_STRIPPED |
                            RX_FLAG_MMIC_STRIPPED;
    }

    skb_queue_walk(amsdu, msdu) {
        ath10k_htt_rx_h_csum_offload(msdu);
        ath10k_htt_rx_h_undecap(ar, msdu, status, first_hdr, enctype,
                                is_decrypted);

        /* Undecapping involves copying the original 802.11 header back
         * to sk_buff. If frame is protected and hardware has decrypted
         * it then remove the protected bit.
         */
        if (!is_decrypted) {
            continue;
        }
        if (is_mgmt) {
            continue;
        }

        hdr = (void*)msdu->data;
        hdr->frame_control &= ~IEEE80211_FCTL_PROTECTED;
    }
}

static void ath10k_htt_rx_h_deliver(struct ath10k* ar,
                                    struct sk_buff_head* amsdu,
                                    struct ieee80211_rx_status* status) {
    struct sk_buff* msdu;

    while ((msdu = __skb_dequeue(amsdu))) {
        /* Setup per-MSDU flags */
        if (skb_queue_empty(amsdu)) {
            status->flag &= ~RX_FLAG_AMSDU_MORE;
        } else {
            status->flag |= RX_FLAG_AMSDU_MORE;
        }

        ath10k_process_rx(ar, status, msdu);
    }
}

static int ath10k_unchain_msdu(struct sk_buff_head* amsdu) {
    struct sk_buff* skb, *first;
    int space;
    int total_len = 0;

    /* TODO:  Might could optimize this by using
     * skb_try_coalesce or similar method to
     * decrease copying, or maybe get mac80211 to
     * provide a way to just receive a list of
     * skb?
     */

    first = __skb_dequeue(amsdu);

    /* Allocate total length all at once. */
    skb_queue_walk(amsdu, skb)
    total_len += skb->len;

    space = total_len - skb_tailroom(first);
    if ((space > 0) &&
            (pskb_expand_head(first, 0, space, GFP_ATOMIC) < 0)) {
        /* TODO:  bump some rx-oom error stat */
        /* put it back together so we can free the
         * whole list at once.
         */
        __skb_queue_head(amsdu, first);
        return -1;
    }

    /* Walk list again, copying contents into
     * msdu_head
     */
    while ((skb = __skb_dequeue(amsdu))) {
        skb_copy_from_linear_data(skb, skb_put(first, skb->len),
                                  skb->len);
        dev_kfree_skb_any(skb);
    }

    __skb_queue_head(amsdu, first);
    return 0;
}

static void ath10k_htt_rx_h_unchain(struct ath10k* ar,
                                    struct sk_buff_head* amsdu) {
    struct sk_buff* first;
    struct htt_rx_desc* rxd;
    enum rx_msdu_decap_format decap;

    first = skb_peek(amsdu);
    rxd = (void*)first->data - sizeof(*rxd);
    decap = MS(rxd->msdu_start.common.info1,
               RX_MSDU_START_INFO1_DECAP_FORMAT);

    /* FIXME: Current unchaining logic can only handle simple case of raw
     * msdu chaining. If decapping is other than raw the chaining may be
     * more complex and this isn't handled by the current code. Don't even
     * try re-constructing such frames - it'll be pretty much garbage.
     */
    if (decap != RX_MSDU_DECAP_RAW ||
            skb_queue_len(amsdu) != 1 + rxd->frag_info.ring2_more_count) {
        __skb_queue_purge(amsdu);
        return;
    }

    ath10k_unchain_msdu(amsdu);
}

static bool ath10k_htt_rx_amsdu_allowed(struct ath10k* ar,
                                        struct sk_buff_head* amsdu,
                                        struct ieee80211_rx_status* rx_status) {
    /* FIXME: It might be a good idea to do some fuzzy-testing to drop
     * invalid/dangerous frames.
     */

    if (!rx_status->freq) {
        ath10k_warn("no channel configured; ignoring frame(s)!\n");
        return false;
    }

    if (BITARR_TEST(&ar->dev_flags, ATH10K_CAC_RUNNING)) {
        ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx cac running\n");
        return false;
    }

    return true;
}

static void ath10k_htt_rx_h_filter(struct ath10k* ar,
                                   struct sk_buff_head* amsdu,
                                   struct ieee80211_rx_status* rx_status) {
    if (skb_queue_empty(amsdu)) {
        return;
    }

    if (ath10k_htt_rx_amsdu_allowed(ar, amsdu, rx_status)) {
        return;
    }

    __skb_queue_purge(amsdu);
}

static zx_status_t ath10k_htt_rx_handle_amsdu(struct ath10k_htt* htt) {
    struct ath10k* ar = htt->ar;
    struct ieee80211_rx_status* rx_status = &htt->rx_status;
    struct sk_buff_head amsdu;
    int ret, num_msdus;

    __skb_queue_head_init(&amsdu);

    mtx_lock(&htt->rx_ring.lock);
    if (htt->rx_confused) {
        mtx_unlock(&htt->rx_ring.lock);
        return -EIO;
    }
    ret = ath10k_htt_rx_amsdu_pop(htt, &amsdu);
    mtx_unlock(&htt->rx_ring.lock);

    if (ret < 0) {
        ath10k_warn("rx ring became corrupted: %d\n", ret);
        __skb_queue_purge(&amsdu);
        /* FIXME: It's probably a good idea to reboot the
         * device instead of leaving it inoperable.
         */
        htt->rx_confused = true;
        return ret;
    }

    num_msdus = skb_queue_len(&amsdu);
    ath10k_htt_rx_h_ppdu(ar, &amsdu, rx_status, 0xffff);

    /* only for ret = 1 indicates chained msdus */
    if (ret > 0) {
        ath10k_htt_rx_h_unchain(ar, &amsdu);
    }

    ath10k_htt_rx_h_filter(ar, &amsdu, rx_status);
    ath10k_htt_rx_h_mpdu(ar, &amsdu, rx_status);
    ath10k_htt_rx_h_deliver(ar, &amsdu, rx_status);

    return num_msdus;
}

static void ath10k_htt_rx_proc_rx_ind(struct ath10k_htt* htt,
                                      struct htt_rx_indication* rx) {
    struct ath10k* ar = htt->ar;
    struct htt_rx_indication_mpdu_range* mpdu_ranges;
    int num_mpdu_ranges;
    int i, mpdu_count = 0;

    num_mpdu_ranges = MS(rx->hdr.info1,
                         HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);
    mpdu_ranges = htt_rx_ind_get_mpdu_ranges(rx);

    ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx ind: ",
                    rx, sizeof(*rx) +
                    (sizeof(struct htt_rx_indication_mpdu_range) *
                     num_mpdu_ranges));

    for (i = 0; i < num_mpdu_ranges; i++) {
        mpdu_count += mpdu_ranges[i].mpdu_count;
    }

    atomic_fetch_add(&htt->num_mpdus_ready, mpdu_count);
}
#endif // NEEDS PORTING

static void ath10k_htt_rx_tx_compl_ind(struct ath10k* ar,
                                       struct ath10k_msg_buf* buf) {
    struct ath10k_htt* htt = &ar->htt;
    struct htt_resp* resp = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_HTT_RESP);
    struct htt_tx_done tx_done = {};
    int status = MS(resp->data_tx_completion.flags, HTT_DATA_TX_STATUS);
    uint16_t msdu_id;
    int i;

    switch (status) {
    case HTT_DATA_TX_STATUS_NO_ACK:
        tx_done.status = HTT_TX_COMPL_STATE_NOACK;
        break;
    case HTT_DATA_TX_STATUS_OK:
        tx_done.status = HTT_TX_COMPL_STATE_ACK;
        break;
    case HTT_DATA_TX_STATUS_DISCARD:
    case HTT_DATA_TX_STATUS_POSTPONE:
    case HTT_DATA_TX_STATUS_DOWNLOAD_FAIL:
        tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
        break;
    default:
        ath10k_warn("unhandled tx completion status %d\n", status);
        tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
        break;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx completion num_msdus %d\n",
               resp->data_tx_completion.num_msdus);

    for (i = 0; i < resp->data_tx_completion.num_msdus; i++) {
        msdu_id = resp->data_tx_completion.msdus[i];
        tx_done.msdu_id = msdu_id;
        ath10k_txrx_tx_unref(htt, &tx_done);
    }
}

#if 0 // NEEDS PORTING
static void ath10k_htt_rx_addba(struct ath10k* ar, struct htt_resp* resp) {
    struct htt_rx_addba* ev = &resp->rx_addba;
    struct ath10k_peer* peer;
    struct ath10k_vif* arvif;
    uint16_t info0, tid, peer_id;

    info0 = ev->info0;
    tid = MS(info0, HTT_RX_BA_INFO0_TID);
    peer_id = MS(info0, HTT_RX_BA_INFO0_PEER_ID);

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt rx addba tid %hu peer_id %hu size %hhu\n",
               tid, peer_id, ev->window_size);

    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find_by_id(ar, peer_id);
    if (!peer) {
        ath10k_warn("received addba event for invalid peer_id: %hu\n",
                    peer_id);
        mtx_unlock(&ar->data_lock);
        return;
    }

    arvif = ath10k_get_arvif(ar, peer->vdev_id);
    if (!arvif) {
        ath10k_warn("received addba event for invalid vdev_id: %u\n",
                    peer->vdev_id);
        mtx_unlock(&ar->data_lock);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt rx start rx ba session sta %pM tid %hu size %hhu\n",
               peer->addr, tid, ev->window_size);

    ieee80211_start_rx_ba_session_offl(arvif->vif, peer->addr, tid);
    mtx_unlock(&ar->data_lock);
}

static void ath10k_htt_rx_delba(struct ath10k* ar, struct htt_resp* resp) {
    struct htt_rx_delba* ev = &resp->rx_delba;
    struct ath10k_peer* peer;
    struct ath10k_vif* arvif;
    uint16_t info0, tid, peer_id;

    info0 = ev->info0;
    tid = MS(info0, HTT_RX_BA_INFO0_TID);
    peer_id = MS(info0, HTT_RX_BA_INFO0_PEER_ID);

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt rx delba tid %hu peer_id %hu\n",
               tid, peer_id);

    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find_by_id(ar, peer_id);
    if (!peer) {
        ath10k_warn("received addba event for invalid peer_id: %hu\n",
                    peer_id);
        mtx_unlock(&ar->data_lock);
        return;
    }

    arvif = ath10k_get_arvif(ar, peer->vdev_id);
    if (!arvif) {
        ath10k_warn("received addba event for invalid vdev_id: %u\n",
                    peer->vdev_id);
        mtx_unlock(&ar->data_lock);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt rx stop rx ba session sta %pM tid %hu\n",
               peer->addr, tid);

    ieee80211_stop_rx_ba_session_offl(arvif->vif, peer->addr, tid);
    mtx_unlock(&ar->data_lock);
}

static int ath10k_htt_rx_extract_amsdu(struct sk_buff_head* list,
                                       struct sk_buff_head* amsdu) {
    struct sk_buff* msdu;
    struct htt_rx_desc* rxd;

    if (skb_queue_empty(list)) {
        return -ENOBUFS;
    }

    if (COND_WARN(!skb_queue_empty(amsdu))) {
        return -EINVAL;
    }

    while ((msdu = __skb_dequeue(list))) {
        __skb_queue_tail(amsdu, msdu);

        rxd = (void*)msdu->data - sizeof(*rxd);
        if (rxd->msdu_end.common.info0&
                RX_MSDU_END_INFO0_LAST_MSDU) {
            break;
        }
    }

    msdu = skb_peek_tail(amsdu);
    rxd = (void*)msdu->data - sizeof(*rxd);
    if (!(rxd->msdu_end.common.info0&
            RX_MSDU_END_INFO0_LAST_MSDU)) {
        skb_queue_splice_init(amsdu, list);
        return -EAGAIN;
    }

    return 0;
}

static void ath10k_htt_rx_h_rx_offload_prot(struct ieee80211_rx_status* status,
        struct sk_buff* skb) {
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;

    if (!ieee80211_has_protected(hdr->frame_control)) {
        return;
    }

    /* Offloaded frames are already decrypted but firmware insists they are
     * protected in the 802.11 header. Strip the flag.  Otherwise mac80211
     * will drop the frame.
     */

    hdr->frame_control &= ~IEEE80211_FCTL_PROTECTED;
    status->flag |= RX_FLAG_DECRYPTED |
                    RX_FLAG_IV_STRIPPED |
                    RX_FLAG_MMIC_STRIPPED;
}

static int ath10k_htt_rx_h_rx_offload(struct ath10k* ar,
                                      struct sk_buff_head* list) {
    struct ath10k_htt* htt = &ar->htt;
    struct ieee80211_rx_status* status = &htt->rx_status;
    struct htt_rx_offload_msdu* rx;
    struct sk_buff* msdu;
    size_t offset;
    int num_msdu = 0;

    while ((msdu = __skb_dequeue(list))) {
        /* Offloaded frames don't have Rx descriptor. Instead they have
         * a short meta information header.
         */

        rx = (void*)msdu->data;

        skb_put(msdu, sizeof(*rx));
        skb_pull(msdu, sizeof(*rx));

        if (skb_tailroom(msdu) < rx->msdu_len) {
            ath10k_warn("dropping frame: offloaded rx msdu is too long!\n");
            dev_kfree_skb_any(msdu);
            continue;
        }

        skb_put(msdu, rx->msdu_len);

        /* Offloaded rx header length isn't multiple of 2 nor 4 so the
         * actual payload is unaligned. Align the frame.  Otherwise
         * mac80211 complains.  This shouldn't reduce performance much
         * because these offloaded frames are rare.
         */
        offset = 4 - ((unsigned long)msdu->data & 3);
        skb_put(msdu, offset);
        memmove(msdu->data + offset, msdu->data, msdu->len);
        skb_pull(msdu, offset);

        /* FIXME: The frame is NWifi. Re-construct QoS Control
         * if possible later.
         */

        memset(status, 0, sizeof(*status));
        status->flag |= RX_FLAG_NO_SIGNAL_VAL;

        ath10k_htt_rx_h_rx_offload_prot(status, msdu);
        ath10k_htt_rx_h_channel(ar, status, NULL, rx->vdev_id);
        ath10k_process_rx(ar, status, msdu);
        num_msdu++;
    }
    return num_msdu;
}
#endif // NEEDS PORTING

static zx_status_t ath10k_htt_rx_in_ord_ind(struct ath10k* ar,
                                            struct ath10k_msg_buf* buf) {
    struct ath10k_htt* htt = &ar->htt;
    struct htt_resp* resp = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_HTT_RESP);

    for (unsigned msdu_ndx = 0; msdu_ndx < resp->rx_in_ord_ind.msdu_count; msdu_ndx++) {
        struct htt_rx_in_ord_msdu_desc* desc = &resp->rx_in_ord_ind.msdu_descs[msdu_ndx];
        uint32_t paddr = desc->msdu_paddr;

        struct ath10k_msg_buf* msdu = ath10k_htt_rx_pop_paddr(htt, paddr);
        size_t msdu_len = desc->msdu_len;
        msdu->type = ATH10K_MSG_TYPE_HTT_RX;
        msdu->used = sizeof(struct htt_rx_desc) + msdu_len;

        struct htt_rx_desc* rx_desc = ath10k_msg_buf_get_header(msdu, ATH10K_MSG_TYPE_HTT_RX);
        wlan_rx_info_t rx_info = {};
        memcpy(&rx_info.chan, &ar->rx_channel, sizeof(wlan_channel_t));
        // TODO: fill in rx_info from rx_desc
        ar->wlanmac.ifc->recv(ar->wlanmac.cookie, 0, rx_desc->msdu_payload, msdu_len, &rx_info);
        ath10k_msg_buf_free(msdu);
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static zx_status_t ath10k_htt_rx_in_ord_ind(struct ath10k* ar,
                                            struct ath10k_msg_buf* buf,
                                            int* num_msdus) {
    struct ath10k_htt* htt = &ar->htt;
    buf->type = ATH10K_MSG_TYPE_HTT_RESP_RX_IN_ORD_IND;
    struct htt_resp* resp = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_HTT_RESP);
    uint16_t peer_id;
    uint16_t msdu_count;
    uint8_t vdev_id;
    uint8_t tid;
    bool offload;
    bool frag;
    zx_status_t status;
    list_node_t list;

    struct ieee80211_rx_status* status = &htt->rx_status;
    list_node_t amsdu;
    int num_msdus = 0;

    ASSERT_MTX_HELD(&htt->rx_ring.lock);

    if (htt->rx_confused) {
        return ZX_ERR_INTERNAL;
    }

    peer_id = resp->rx_in_ord_ind.peer_id;
    msdu_count = resp->rx_in_ord_ind.msdu_count;
    vdev_id = resp->rx_in_ord_ind.vdev_id;
    tid = SM(resp->rx_in_ord_ind.info, HTT_RX_IN_ORD_IND_INFO_TID);
    offload = !!(resp->rx_in_ord_ind.info &
                 HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK);
    frag = !!(resp->rx_in_ord_ind.info & HTT_RX_IN_ORD_IND_INFO_FRAG_MASK);

    ath10k_info("htt rx in ord vdev %i peer %i tid %i offload %i frag %i msdu count %i\n",
                vdev_id, peer_id, tid, offload, frag, msdu_count);

    size_t data_len = buf->used - ath10k_msg_buf_get_payload_offset(buf->type);
    if (data_len < msdu_count * sizeof(*resp->rx_in_ord_ind.msdu_descs)) {
        ath10k_warn("dropping invalid in order rx indication\n");
        return ZX_ERR_INVALID_ARGS;
    }

    /* The event can deliver more than 1 A-MSDU. Each A-MSDU is later
     * extracted and processed.
     */
    list_initialize(&list);
    status = ath10k_htt_rx_pop_paddr_list(htt, &resp->rx_in_ord_ind, &list);
    if (status != ZX_OK) {
        ath10k_warn("failed to pop paddr list: %s\n", zx_status_get_string(status));
        htt->rx_confused = true;
        return ZX_ERR_INTERNAL;
    }

    /* Offloaded frames are very different and need to be handled
     * separately.
     */
    ZX_DEBUG_ASSERT(!offload);
    if (offload) {
        *num_msdus = ath10k_htt_rx_h_rx_offload(ar, &list);
    }

    while (!skb_queue_empty(&list)) {
        list_initialize(&amsdu);
        ret = ath10k_htt_rx_extract_amsdu(&list, &amsdu);
        switch (ret) {
        case 0:
            /* Note: The in-order indication may report interleaved
             * frames from different PPDUs meaning reported rx rate
             * to mac80211 isn't accurate/reliable. It's still
             * better to report something than nothing though. This
             * should still give an idea about rx rate to the user.
             */
            (*num_msdus) += skb_queue_len(&amsdu);
            ath10k_htt_rx_h_ppdu(ar, &amsdu, status, vdev_id);
            ath10k_htt_rx_h_filter(ar, &amsdu, status);
            ath10k_htt_rx_h_mpdu(ar, &amsdu, status);
            ath10k_htt_rx_h_deliver(ar, &amsdu, status);
            break;
        case -EAGAIN:
        /* fall through */
        default:
            /* Should not happen. */
            ath10k_warn("failed to extract amsdu: %d\n", ret);
            htt->rx_confused = true;
            __skb_queue_purge(&list);
            return ZX_ERR_INTERNAL;
        }
    }
    return ZX_OK;
}

static void ath10k_htt_rx_tx_fetch_resp_id_confirm(struct ath10k* ar,
        const uint32_t* resp_ids,
        int num_resp_ids) {
    int i;
    uint32_t resp_id;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm num_resp_ids %d\n",
               num_resp_ids);

    for (i = 0; i < num_resp_ids; i++) {
        resp_id = resp_ids[i];

        ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm resp_id %u\n",
                   resp_id);

        /* TODO: free resp_id */
    }
}

static void ath10k_htt_rx_tx_fetch_ind(struct ath10k* ar, struct sk_buff* skb) {
    struct ieee80211_hw* hw = ar->hw;
    struct ieee80211_txq* txq;
    struct htt_resp* resp = (struct htt_resp*)skb->data;
    struct htt_tx_fetch_record* record;
    size_t len;
    size_t max_num_bytes;
    size_t max_num_msdus;
    size_t num_bytes;
    size_t num_msdus;
    const uint32_t* resp_ids;
    uint16_t num_records;
    uint16_t num_resp_ids;
    uint16_t peer_id;
    uint8_t tid;
    int ret;
    int i;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch ind\n");

    len = sizeof(resp->hdr) + sizeof(resp->tx_fetch_ind);
    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_fetch_ind event: buffer too short\n");
        return;
    }

    num_records = resp->tx_fetch_ind.num_records;
    num_resp_ids = resp->tx_fetch_ind.num_resp_ids;

    len += sizeof(resp->tx_fetch_ind.records[0]) * num_records;
    len += sizeof(resp->tx_fetch_ind.resp_ids[0]) * num_resp_ids;

    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_fetch_ind event: too many records/resp_ids\n");
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch ind num records %hu num resps %hu seq %hu\n",
               num_records, num_resp_ids,
               resp->tx_fetch_ind.fetch_seq_num);

    if (!ar->htt.tx_q_state.enabled) {
        ath10k_warn("received unexpected tx_fetch_ind event: not enabled\n");
        return;
    }

    if (ar->htt.tx_q_state.mode == HTT_TX_MODE_SWITCH_PUSH) {
        ath10k_warn("received unexpected tx_fetch_ind event: in push mode\n");
        return;
    }

    rcu_read_lock();

    for (i = 0; i < num_records; i++) {
        record = &resp->tx_fetch_ind.records[i];
        peer_id = MS(record->info,
                     HTT_TX_FETCH_RECORD_INFO_PEER_ID);
        tid = MS(record->info,
                 HTT_TX_FETCH_RECORD_INFO_TID);
        max_num_msdus = record->num_msdus;
        max_num_bytes = record->num_bytes;

        ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch record %i peer_id %hu tid %hhu msdus %zu bytes %zu\n",
                   i, peer_id, tid, max_num_msdus, max_num_bytes);

        if (unlikely(peer_id >= ar->htt.tx_q_state.num_peers) ||
                unlikely(tid >= ar->htt.tx_q_state.num_tids)) {
            ath10k_warn("received out of range peer_id %hu tid %hhu\n",
                        peer_id, tid);
            continue;
        }

        mtx_lock(&ar->data_lock);
        txq = ath10k_mac_txq_lookup(ar, peer_id, tid);
        mtx_unlock(&ar->data_lock);

        /* It is okay to release the lock and use txq because RCU read
         * lock is held.
         */

        if (unlikely(!txq)) {
            ath10k_warn("failed to lookup txq for peer_id %hu tid %hhu\n",
                        peer_id, tid);
            continue;
        }

        num_msdus = 0;
        num_bytes = 0;

        while (num_msdus < max_num_msdus &&
                num_bytes < max_num_bytes) {
            ret = ath10k_mac_tx_push_txq(hw, txq);
            if (ret < 0) {
                break;
            }

            num_msdus++;
            num_bytes += ret;
        }

        record->num_msdus = num_msdus;
        record->num_bytes = num_bytes;

        ath10k_htt_tx_txq_recalc(hw, txq);
    }

    rcu_read_unlock();

    resp_ids = ath10k_htt_get_tx_fetch_ind_resp_ids(&resp->tx_fetch_ind);
    ath10k_htt_rx_tx_fetch_resp_id_confirm(ar, resp_ids, num_resp_ids);

    ret = ath10k_htt_tx_fetch_resp(ar,
                                   resp->tx_fetch_ind.token,
                                   resp->tx_fetch_ind.fetch_seq_num,
                                   resp->tx_fetch_ind.records,
                                   num_records);
    if (unlikely(ret)) {
        ath10k_warn("failed to submit tx fetch resp for token 0x%08x: %d\n",
                    resp->tx_fetch_ind.token, ret);
        /* FIXME: request fw restart */
    }

    ath10k_htt_tx_txq_sync(ar);
}

static void ath10k_htt_rx_tx_fetch_confirm(struct ath10k* ar,
        struct sk_buff* skb) {
    const struct htt_resp* resp = (void*)skb->data;
    size_t len;
    int num_resp_ids;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm\n");

    len = sizeof(resp->hdr) + sizeof(resp->tx_fetch_confirm);
    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_fetch_confirm event: buffer too short\n");
        return;
    }

    num_resp_ids = resp->tx_fetch_confirm.num_resp_ids;
    len += sizeof(resp->tx_fetch_confirm.resp_ids[0]) * num_resp_ids;

    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_fetch_confirm event: resp_ids buffer overflow\n");
        return;
    }

    ath10k_htt_rx_tx_fetch_resp_id_confirm(ar,
                                           resp->tx_fetch_confirm.resp_ids,
                                           num_resp_ids);
}

static void ath10k_htt_rx_tx_mode_switch_ind(struct ath10k* ar,
        struct sk_buff* skb) {
    const struct htt_resp* resp = (void*)skb->data;
    const struct htt_tx_mode_switch_record* record;
    struct ieee80211_txq* txq;
    struct ath10k_txq* artxq;
    size_t len;
    size_t num_records;
    enum htt_tx_mode_switch_mode mode;
    bool enable;
    uint16_t info0;
    uint16_t info1;
    uint16_t threshold;
    uint16_t peer_id;
    uint8_t tid;
    int i;

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx mode switch ind\n");

    len = sizeof(resp->hdr) + sizeof(resp->tx_mode_switch_ind);
    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_mode_switch_ind event: buffer too short\n");
        return;
    }

    info0 = resp->tx_mode_switch_ind.info0;
    info1 = resp->tx_mode_switch_ind.info1;

    enable = !!(info0 & HTT_TX_MODE_SWITCH_IND_INFO0_ENABLE);
    num_records = MS(info0, HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD);
    mode = MS(info1, HTT_TX_MODE_SWITCH_IND_INFO1_MODE);
    threshold = MS(info1, HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD);

    ath10k_dbg(ar, ATH10K_DBG_HTT,
               "htt rx tx mode switch ind info0 0x%04hx info1 0x%04hx enable %d num records %zd mode %d threshold %hu\n",
               info0, info1, enable, num_records, mode, threshold);

    len += sizeof(resp->tx_mode_switch_ind.records[0]) * num_records;

    if (unlikely(skb->len < len)) {
        ath10k_warn("received corrupted tx_mode_switch_mode_ind event: too many records\n");
        return;
    }

    switch (mode) {
    case HTT_TX_MODE_SWITCH_PUSH:
    case HTT_TX_MODE_SWITCH_PUSH_PULL:
        break;
    default:
        ath10k_warn("received invalid tx_mode_switch_mode_ind mode %d, ignoring\n",
                    mode);
        return;
    }

    if (!enable) {
        return;
    }

    ar->htt.tx_q_state.enabled = enable;
    ar->htt.tx_q_state.mode = mode;
    ar->htt.tx_q_state.num_push_allowed = threshold;

    rcu_read_lock();

    for (i = 0; i < num_records; i++) {
        record = &resp->tx_mode_switch_ind.records[i];
        info0 = record->info0;
        peer_id = MS(info0, HTT_TX_MODE_SWITCH_RECORD_INFO0_PEER_ID);
        tid = MS(info0, HTT_TX_MODE_SWITCH_RECORD_INFO0_TID);

        if (unlikely(peer_id >= ar->htt.tx_q_state.num_peers) ||
                unlikely(tid >= ar->htt.tx_q_state.num_tids)) {
            ath10k_warn("received out of range peer_id %hu tid %hhu\n",
                        peer_id, tid);
            continue;
        }

        mtx_lock(&ar->data_lock);
        txq = ath10k_mac_txq_lookup(ar, peer_id, tid);
        mtx_unlock(&ar->data_lock);

        /* It is okay to release the lock and use txq because RCU read
         * lock is held.
         */

        if (unlikely(!txq)) {
            ath10k_warn("failed to lookup txq for peer_id %hu tid %hhu\n",
                        peer_id, tid);
            continue;
        }

        mtx_lock(&ar->htt.tx_lock);
        artxq = (void*)txq->drv_priv;
        artxq->num_push_allowed = record->num_max_msdus;
        mtx_unlock(&ar->htt.tx_lock);
    }

    rcu_read_unlock();

    ath10k_mac_tx_push_pending(ar);
}
#endif // NEEDS PORTING

void ath10k_htt_htc_t2h_msg_handler(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    bool release;

    release = ath10k_htt_t2h_msg_handler(ar, msg_buf);

    /* Free the indication buffer */
    if (release) {
        ath10k_msg_buf_free(msg_buf);
    }
}

#if 0 // NEEDS PORTING
static inline bool is_valid_legacy_rate(uint8_t rate) {
    static const uint8_t legacy_rates[] = {1, 2, 5, 11, 6, 9, 12,
                                           18, 24, 36, 48, 54
                                          };
    int i;

    for (i = 0; i < countof(legacy_rates); i++) {
        if (rate == legacy_rates[i]) {
            return true;
        }
    }

    return false;
}

static void
ath10k_update_per_peer_tx_stats(struct ath10k* ar,
                                struct ieee80211_sta* sta,
                                struct ath10k_per_peer_tx_stats* peer_stats) {
    struct ath10k_sta* arsta = (struct ath10k_sta*)sta->drv_priv;
    uint8_t rate = 0, sgi;
    struct rate_info txrate;

    ASSERT_MTX_HELD(&ar->data_lock);

    txrate.flags = ATH10K_HW_PREAMBLE(peer_stats->ratecode);
    txrate.bw = ATH10K_HW_BW(peer_stats->flags);
    txrate.nss = ATH10K_HW_NSS(peer_stats->ratecode);
    txrate.mcs = ATH10K_HW_MCS_RATE(peer_stats->ratecode);
    sgi = ATH10K_HW_GI(peer_stats->flags);

    if (txrate.flags == WMI_RATE_PREAMBLE_VHT && txrate.mcs > 9) {
        ath10k_warn("Invalid VHT mcs %hhd peer stats",  txrate.mcs);
        return;
    }

    if (txrate.flags == WMI_RATE_PREAMBLE_HT &&
            (txrate.mcs > 7 || txrate.nss < 1)) {
        ath10k_warn("Invalid HT mcs %hhd nss %hhd peer stats",
                    txrate.mcs, txrate.nss);
        return;
    }

    memset(&arsta->txrate, 0, sizeof(arsta->txrate));

    if (txrate.flags == WMI_RATE_PREAMBLE_CCK ||
            txrate.flags == WMI_RATE_PREAMBLE_OFDM) {
        rate = ATH10K_HW_LEGACY_RATE(peer_stats->ratecode);

        if (!is_valid_legacy_rate(rate)) {
            ath10k_warn("Invalid legacy rate %hhd peer stats",
                        rate);
            return;
        }

        /* This is hacky, FW sends CCK rate 5.5Mbps as 6 */
        rate *= 10;
        if (rate == 60 && txrate.flags == WMI_RATE_PREAMBLE_CCK) {
            rate = rate - 5;
        }
        arsta->txrate.legacy = rate;
    } else if (txrate.flags == WMI_RATE_PREAMBLE_HT) {
        arsta->txrate.flags = RATE_INFO_FLAGS_MCS;
        arsta->txrate.mcs = txrate.mcs + 8 * (txrate.nss - 1);
    } else {
        arsta->txrate.flags = RATE_INFO_FLAGS_VHT_MCS;
        arsta->txrate.mcs = txrate.mcs;
    }

    if (sgi) {
        arsta->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
    }

    arsta->txrate.nss = txrate.nss;
    arsta->txrate.bw = txrate.bw + RATE_INFO_BW_20;
}

static void ath10k_htt_fetch_peer_stats(struct ath10k* ar,
                                        struct sk_buff* skb) {
    struct htt_resp* resp = (struct htt_resp*)skb->data;
    struct ath10k_per_peer_tx_stats* p_tx_stats = &ar->peer_tx_stats;
    struct htt_per_peer_tx_stats_ind* tx_stats;
    struct ieee80211_sta* sta;
    struct ath10k_peer* peer;
    int peer_id, i;
    uint8_t ppdu_len, num_ppdu;

    num_ppdu = resp->peer_tx_stats.num_ppdu;
    ppdu_len = resp->peer_tx_stats.ppdu_len * sizeof(uint32_t);

    if (skb->len < sizeof(struct htt_resp_hdr) + num_ppdu * ppdu_len) {
        ath10k_warn("Invalid peer stats buf length %d\n", skb->len);
        return;
    }

    tx_stats = (struct htt_per_peer_tx_stats_ind*)
               (resp->peer_tx_stats.payload);
    peer_id = tx_stats->peer_id;

    rcu_read_lock();
    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find_by_id(ar, peer_id);
    if (!peer) {
        ath10k_warn("Invalid peer id %d peer stats buffer\n",
                    peer_id);
        goto out;
    }

    sta = peer->sta;
    for (i = 0; i < num_ppdu; i++) {
        tx_stats = (struct htt_per_peer_tx_stats_ind*)
                   (resp->peer_tx_stats.payload + i * ppdu_len);

        p_tx_stats->succ_bytes = tx_stats->succ_bytes;
        p_tx_stats->retry_bytes = tx_stats->retry_bytes;
        p_tx_stats->failed_bytes =
            tx_stats->failed_bytes;
        p_tx_stats->ratecode = tx_stats->ratecode;
        p_tx_stats->flags = tx_stats->flags;
        p_tx_stats->succ_pkts = tx_stats->succ_pkts;
        p_tx_stats->retry_pkts = tx_stats->retry_pkts;
        p_tx_stats->failed_pkts = tx_stats->failed_pkts;

        ath10k_update_per_peer_tx_stats(ar, sta, p_tx_stats);
    }

out:
    mtx_unlock(&ar->data_lock);
    rcu_read_unlock();
}
#endif // NEEDS PORTING

bool ath10k_htt_t2h_msg_handler(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct ath10k_htt* htt = &ar->htt;
    struct htt_resp* resp = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_HTT_RESP);
    zx_status_t status;

    /* confirm alignment */
    if (!IS_ALIGNED((unsigned long)msg_buf->vaddr, 4)) {
        ath10k_warn("unaligned htt message, expect trouble\n");
    }

    ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx, msg_type: 0x%0X\n",
               resp->hdr.msg_type);

    if (resp->hdr.msg_type >= ar->htt.t2h_msg_types_max) {
        ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx, unsupported msg_type: 0x%0X\n max: 0x%0X",
                   resp->hdr.msg_type, ar->htt.t2h_msg_types_max);
        return true;
    }
    enum htt_t2h_msg_type type = ar->htt.t2h_msg_types[resp->hdr.msg_type];

    switch (type) {
    case HTT_T2H_MSG_TYPE_VERSION_CONF: {
        htt->target_version_major = resp->ver_resp.major;
        htt->target_version_minor = resp->ver_resp.minor;
        sync_completion_signal(&htt->target_version_received);
        break;
    }
    case HTT_T2H_MSG_TYPE_RX_IND:
        ath10k_err("HTT_T2H_MSG_TYPE_RX_IND unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_rx_proc_rx_ind(htt, &resp->rx_ind);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_PEER_MAP: {
        ath10k_err("HTT_T2H_MSG_TYPE_PEER_MAP unimplemented\n");
#if 0 // NEEDS PORTING
        struct htt_peer_map_event ev = {
            .vdev_id = resp->peer_map.vdev_id,
            .peer_id = resp->peer_map.peer_id,
        };
        memcpy(ev.addr, resp->peer_map.addr, sizeof(ev.addr));
        ath10k_peer_map_event(htt, &ev);
#endif // NEEDS PORTING
        break;
    }
    case HTT_T2H_MSG_TYPE_PEER_UNMAP: {
        ath10k_err("HTT_T2H_MSG_TYPE_PEER_UNMAP unimplemented\n");
#if 0 // NEEDS PORTING
        struct htt_peer_unmap_event ev = {
            .peer_id = resp->peer_unmap.peer_id,
        };
        ath10k_peer_unmap_event(htt, &ev);
#endif // NEEDS PORTING
        break;
    }
    case HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION: {
        struct htt_tx_done tx_done = {};
        int compl_status = resp->mgmt_tx_completion.status;

        tx_done.msdu_id = resp->mgmt_tx_completion.desc_id;

        switch (compl_status) {
        case HTT_MGMT_TX_STATUS_OK:
            tx_done.status = HTT_TX_COMPL_STATE_ACK;
            break;
        case HTT_MGMT_TX_STATUS_RETRY:
            tx_done.status = HTT_TX_COMPL_STATE_NOACK;
            break;
        case HTT_MGMT_TX_STATUS_DROP:
            tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
            break;
        }

        if (ath10k_txrx_tx_unref(htt, &tx_done) == ZX_OK) {
            mtx_lock(&htt->tx_lock);
            ath10k_htt_tx_mgmt_dec_pending(htt);
            mtx_unlock(&htt->tx_lock);
        }
        break;
    }
    case HTT_T2H_MSG_TYPE_TX_COMPL_IND:
        ath10k_htt_rx_tx_compl_ind(htt->ar, msg_buf);
        break;
    case HTT_T2H_MSG_TYPE_SEC_IND: {
        struct ath10k* ar = htt->ar;
        struct htt_security_indication* ev = &resp->security_indication;

        ath10k_dbg(ar, ATH10K_DBG_HTT,
                   "sec ind peer_id %d unicast %d type %d\n",
                   ev->peer_id,
                   !!(ev->flags & HTT_SECURITY_IS_UNICAST),
                   MS(ev->flags, HTT_SECURITY_TYPE));
        sync_completion_signal(&ar->install_key_done);
        break;
    }
    case HTT_T2H_MSG_TYPE_RX_FRAG_IND: {
        ath10k_err("HTT_T2H_MSG_TYPE_RX_FRAG_IND unimplemented\n");
        ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
                        msg_buf->vaddr, msg_buf->used);
        atomic_fetch_add(&htt->num_mpdus_ready, 1);
        break;
    }
    case HTT_T2H_MSG_TYPE_TEST:
        break;
    case HTT_T2H_MSG_TYPE_STATS_CONF:
        ath10k_err("HTT_T2H_MSG_TYPE_STATS_CONF unimplemented\n");
        break;
    case HTT_T2H_MSG_TYPE_TX_INSPECT_IND:
        /* Firmware can return tx frames if it's unable to fully
         * process them and suspects host may be able to fix it. ath10k
         * sends all tx frames as already inspected so this shouldn't
         * happen unless fw has a bug.
         */
        ath10k_warn("received an unexpected htt tx inspect event\n");
        break;
    case HTT_T2H_MSG_TYPE_RX_ADDBA:
        ath10k_err("HTT_T2H_MSG_TYPE_RX_ADDBA unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_rx_addba(ar, resp);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_RX_DELBA:
        ath10k_err("HTT_T2H_MSG_TYPE_RX_DELBA unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_rx_delba(ar, resp);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_PKTLOG: {
        ath10k_err("HTT_T2H_MSG_TYPE_PKTLOG unimplemented\n");
#if 0 // NEEDS PORTING
        trace_ath10k_htt_pktlog(ar, resp->pktlog_msg.payload,
                                skb->len -
                                offsetof(struct htt_resp,
                                         pktlog_msg.payload));
#endif // NEEDS PORTING
        break;
    }
    case HTT_T2H_MSG_TYPE_RX_FLUSH: {
        /* TODO: Verify that we can ignore this event because mac takes care of Rx
         * aggregation reordering
         */
        break;
    }
    case HTT_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND: {
        mtx_lock(&htt->rx_ring.lock);
        status = ath10k_htt_rx_in_ord_ind(ar, msg_buf);
        mtx_unlock(&htt->rx_ring.lock);
        ath10k_htt_rx_msdu_buff_replenish(htt);
        if (status != ZX_OK) {
            return false;
        }
        break;
    }
    case HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND:
        break;
    case HTT_T2H_MSG_TYPE_CHAN_CHANGE: {
        ath10k_err("HTT_T2H_MSG_TYPE_CHAN_CHANGE unimplemented\n");
#if 0 // NEEDS PORTING
        uint32_t phymode = resp->chan_change.phymode;
        uint32_t freq = resp->chan_change.freq;

        ar->tgt_oper_chan = ieee80211_get_channel(ar->hw->wiphy, freq);
        ath10k_dbg(ar, ATH10K_DBG_HTT,
                   "htt chan change freq %u phymode %s\n",
                   freq, ath10k_wmi_phymode_str(phymode));
#endif // NEEDS PORTING
        break;
    }
    case HTT_T2H_MSG_TYPE_AGGR_CONF:
        break;
    case HTT_T2H_MSG_TYPE_TX_FETCH_IND: {
        ath10k_err("HTT_T2H_MSG_TYPE_TX_FETCH_IND unimplemented\n");
        break;
    }
    case HTT_T2H_MSG_TYPE_TX_FETCH_CONFIRM:
        ath10k_err("HTT_T2H_MSG_TYPE_TX_FETCH_CONFIRM unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_rx_tx_fetch_confirm(ar, skb);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_TX_MODE_SWITCH_IND:
        ath10k_err("HTT_T2H_MSG_TYPE_TX_MODE_SWITCH_IND unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_rx_tx_mode_switch_ind(ar, skb);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_PEER_STATS:
        ath10k_err("HTT_T2H_MSG_TYPE_PEER_STATS unimplemented\n");
#if 0 // NEEDS PORTING
        ath10k_htt_fetch_peer_stats(ar, skb);
#endif // NEEDS PORTING
        break;
    case HTT_T2H_MSG_TYPE_EN_STATS:
    default:
        ath10k_warn("htt event (%d) not handled\n",
                    resp->hdr.msg_type);
        ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
                        msg_buf->vaddr, msg_buf->used);
        break;
    }
    return true;
}

void ath10k_htt_rx_pktlog_completion_handler(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    ath10k_msg_buf_free(buf);
}
