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

//#include <linux/etherdevice.h>
//#include <linux/netdevice.h>
//#include <linux/types.h>

#include "flowring.h"

#include <threads.h>

#include "brcmu_utils.h"
#include "bus.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "device.h"
#include "linuxisms.h"
#include "msgbuf.h"
#include "netbuf.h"
#include "proto.h"

#define BRCMF_FLOWRING_HIGH 1024
#define BRCMF_FLOWRING_LOW (BRCMF_FLOWRING_HIGH - 256)
#define BRCMF_FLOWRING_INVALID_IFIDX 0xff

#define BRCMF_FLOWRING_HASH_AP(da, fifo, ifidx) (da[5] * 2 + fifo + ifidx * 16)
#define BRCMF_FLOWRING_HASH_STA(fifo, ifidx) (fifo + ifidx * 16)

static const uint8_t brcmf_flowring_prio2fifo[] = {1, 0, 0, 1, 2, 2, 3, 3};

static bool brcmf_flowring_is_tdls_mac(struct brcmf_flowring* flow, uint8_t mac[ETH_ALEN]) {
    struct brcmf_flowring_tdls_entry* search;

    search = flow->tdls_entry;

    while (search) {
        if (memcmp(search->mac, mac, ETH_ALEN) == 0) {
            return true;
        }
        search = search->next;
    }

    return false;
}

uint32_t brcmf_flowring_lookup(struct brcmf_flowring* flow, uint8_t da[ETH_ALEN], uint8_t prio,
                               uint8_t ifidx) {
    struct brcmf_flowring_hash* hash;
    uint16_t hash_idx;
    uint32_t i;
    bool found;
    bool sta;
    uint8_t fifo;
    uint8_t* mac;

    fifo = brcmf_flowring_prio2fifo[prio];
    sta = (flow->addr_mode[ifidx] == ADDR_INDIRECT);
    mac = da;
    if ((!sta) && (address_is_multicast(da))) {
        mac = (uint8_t*)ALLFFMAC;
        fifo = 0;
    }
    if ((sta) && (flow->tdls_active) && (brcmf_flowring_is_tdls_mac(flow, da))) {
        sta = false;
    }
    hash_idx =
        sta ? BRCMF_FLOWRING_HASH_STA(fifo, ifidx) : BRCMF_FLOWRING_HASH_AP(mac, fifo, ifidx);
    hash_idx &= (BRCMF_FLOWRING_HASHSIZE - 1);
    found = false;
    hash = flow->hash;
    for (i = 0; i < BRCMF_FLOWRING_HASHSIZE; i++) {
        if ((sta || (memcmp(hash[hash_idx].mac, mac, ETH_ALEN) == 0)) &&
                (hash[hash_idx].fifo == fifo) && (hash[hash_idx].ifidx == ifidx)) {
            found = true;
            break;
        }
        hash_idx++;
        hash_idx &= (BRCMF_FLOWRING_HASHSIZE - 1);
    }
    if (found) {
        return hash[hash_idx].flowid;
    }

    return BRCMF_FLOWRING_INVALID_ID;
}

zx_status_t brcmf_flowring_create(struct brcmf_flowring* flow, uint8_t da[ETH_ALEN], uint8_t prio,
                                  uint8_t ifidx) {
    struct brcmf_flowring_ring* ring;
    struct brcmf_flowring_hash* hash;
    uint16_t hash_idx;
    uint32_t i;
    bool found;
    uint8_t fifo;
    bool sta;
    uint8_t* mac;

    fifo = brcmf_flowring_prio2fifo[prio];
    sta = (flow->addr_mode[ifidx] == ADDR_INDIRECT);
    mac = da;
    if ((!sta) && (address_is_multicast(da))) {
        mac = (uint8_t*)ALLFFMAC;
        fifo = 0;
    }
    if ((sta) && (flow->tdls_active) && (brcmf_flowring_is_tdls_mac(flow, da))) {
        sta = false;
    }
    hash_idx =
        sta ? BRCMF_FLOWRING_HASH_STA(fifo, ifidx) : BRCMF_FLOWRING_HASH_AP(mac, fifo, ifidx);
    hash_idx &= (BRCMF_FLOWRING_HASHSIZE - 1);
    found = false;
    hash = flow->hash;
    for (i = 0; i < BRCMF_FLOWRING_HASHSIZE; i++) {
        if ((hash[hash_idx].ifidx == BRCMF_FLOWRING_INVALID_IFIDX) &&
                (address_is_zero(hash[hash_idx].mac))) {
            found = true;
            break;
        }
        hash_idx++;
        hash_idx &= (BRCMF_FLOWRING_HASHSIZE - 1);
    }
    if (found) {
        for (i = 0; i < flow->nrofrings; i++) {
            if (flow->rings[i] == NULL) {
                break;
            }
        }
        if (i == flow->nrofrings) {
            return ZX_ERR_NO_MEMORY;
        }

        ring = calloc(1, sizeof(*ring));
        if (!ring) {
            return ZX_ERR_NO_MEMORY;
        }

        memcpy(hash[hash_idx].mac, mac, ETH_ALEN);
        hash[hash_idx].fifo = fifo;
        hash[hash_idx].ifidx = ifidx;
        hash[hash_idx].flowid = i;

        ring->hash_id = hash_idx;
        ring->status = RING_CLOSED;
        brcmf_netbuf_list_init(&ring->skblist);
        flow->rings[i] = ring;

        return i;
    }
    return BRCMF_FLOWRING_INVALID_ID;
}

uint8_t brcmf_flowring_tid(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_flowring_ring* ring;

    ring = flow->rings[flowid];

    return flow->hash[ring->hash_id].fifo;
}

static void brcmf_flowring_block(struct brcmf_flowring* flow, uint16_t flowid, bool blocked) {
    struct brcmf_flowring_ring* ring;
    struct brcmf_bus* bus_if;
    struct brcmf_pub* drvr;
    struct brcmf_if* ifp;
    bool currently_blocked;
    int i;
    uint8_t ifidx;

    //spin_lock_irqsave(&flow->block_lock, flags);
    pthread_mutex_lock(&irq_callback_lock);

    ring = flow->rings[flowid];
    if (ring->blocked == blocked) {
        //spin_unlock_irqrestore(&flow->block_lock, flags);
        pthread_mutex_unlock(&irq_callback_lock);
        return;
    }
    ifidx = brcmf_flowring_ifidx_get(flow, flowid);

    currently_blocked = false;
    for (i = 0; i < flow->nrofrings; i++) {
        if ((flow->rings[i]) && (i != flowid)) {
            ring = flow->rings[i];
            if ((ring->status == RING_OPEN) && (brcmf_flowring_ifidx_get(flow, i) == ifidx)) {
                if (ring->blocked) {
                    currently_blocked = true;
                    break;
                }
            }
        }
    }
    flow->rings[flowid]->blocked = blocked;
    if (currently_blocked) {
        //spin_unlock_irqrestore(&flow->block_lock, flags);
        pthread_mutex_unlock(&irq_callback_lock);
        return;
    }

    bus_if = dev_get_drvdata(flow->dev);
    drvr = bus_if->drvr;
    ifp = brcmf_get_ifp(drvr, ifidx);
    brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_FLOW, blocked);

    //spin_unlock_irqrestore(&flow->block_lock, flags);
    pthread_mutex_unlock(&irq_callback_lock);
}

void brcmf_flowring_delete(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_bus* bus_if = dev_get_drvdata(flow->dev);
    struct brcmf_flowring_ring* ring;
    struct brcmf_if* ifp;
    uint16_t hash_idx;
    uint8_t ifidx;
    struct brcmf_netbuf* skb;

    ring = flow->rings[flowid];
    if (!ring) {
        return;
    }

    ifidx = brcmf_flowring_ifidx_get(flow, flowid);
    ifp = brcmf_get_ifp(bus_if->drvr, ifidx);

    brcmf_flowring_block(flow, flowid, false);
    hash_idx = ring->hash_id;
    flow->hash[hash_idx].ifidx = BRCMF_FLOWRING_INVALID_IFIDX;
    fill_with_zero_addr(flow->hash[hash_idx].mac);
    flow->rings[flowid] = NULL;

    skb = skb_dequeue(&ring->skblist);
    while (skb) {
        brcmf_txfinalize(ifp, skb, false);
        skb = skb_dequeue(&ring->skblist);
    }

    free(ring);
}

uint32_t brcmf_flowring_enqueue(struct brcmf_flowring* flow, uint16_t flowid,
                                struct brcmf_netbuf* skb) {
    struct brcmf_flowring_ring* ring;

    ring = flow->rings[flowid];

    skb_queue_tail(&ring->skblist, skb);

    if (!ring->blocked && (skb_queue_len(&ring->skblist) > BRCMF_FLOWRING_HIGH)) {
        brcmf_flowring_block(flow, flowid, true);
        brcmf_dbg(MSGBUF, "Flowcontrol: BLOCK for ring %d\n", flowid);
        /* To prevent (work around) possible race condition, check
         * queue len again. It is also possible to use locking to
         * protect, but that is undesirable for every enqueue and
         * dequeue. This simple check will solve a possible race
         * condition if it occurs.
         */
        if (skb_queue_len(&ring->skblist) < BRCMF_FLOWRING_LOW) {
            brcmf_flowring_block(flow, flowid, false);
        }
    }
    return skb_queue_len(&ring->skblist);
}

struct brcmf_netbuf* brcmf_flowring_dequeue(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_flowring_ring* ring;
    struct brcmf_netbuf* skb;

    ring = flow->rings[flowid];
    if (ring->status != RING_OPEN) {
        return NULL;
    }

    skb = skb_dequeue(&ring->skblist);

    if (ring->blocked && (skb_queue_len(&ring->skblist) < BRCMF_FLOWRING_LOW)) {
        brcmf_flowring_block(flow, flowid, false);
        brcmf_dbg(MSGBUF, "Flowcontrol: OPEN for ring %d\n", flowid);
    }

    return skb;
}

void brcmf_flowring_reinsert(struct brcmf_flowring* flow, uint16_t flowid,
                             struct brcmf_netbuf* skb) {
    struct brcmf_flowring_ring* ring;

    ring = flow->rings[flowid];

    skb_queue_head(&ring->skblist, skb);
}

uint32_t brcmf_flowring_qlen(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_flowring_ring* ring;

    ring = flow->rings[flowid];
    if (!ring) {
        return 0;
    }

    if (ring->status != RING_OPEN) {
        return 0;
    }

    return skb_queue_len(&ring->skblist);
}

void brcmf_flowring_open(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_flowring_ring* ring;

    ring = flow->rings[flowid];
    if (!ring) {
        brcmf_err("Ring NULL, for flowid %d\n", flowid);
        return;
    }

    ring->status = RING_OPEN;
}

uint8_t brcmf_flowring_ifidx_get(struct brcmf_flowring* flow, uint16_t flowid) {
    struct brcmf_flowring_ring* ring;
    uint16_t hash_idx;

    ring = flow->rings[flowid];
    hash_idx = ring->hash_id;

    return flow->hash[hash_idx].ifidx;
}

struct brcmf_flowring* brcmf_flowring_attach(struct brcmf_device* dev, uint16_t nrofrings) {
    struct brcmf_flowring* flow;
    uint32_t i;

    flow = calloc(1, sizeof(*flow));
    if (flow) {
        flow->dev = dev;
        flow->nrofrings = nrofrings;
        //spin_lock_init(&flow->block_lock);
        for (i = 0; i < ARRAY_SIZE(flow->addr_mode); i++) {
            flow->addr_mode[i] = ADDR_INDIRECT;
        }
        for (i = 0; i < ARRAY_SIZE(flow->hash); i++) {
            flow->hash[i].ifidx = BRCMF_FLOWRING_INVALID_IFIDX;
        }
        flow->rings = calloc(nrofrings, sizeof(*flow->rings));
        if (!flow->rings) {
            free(flow);
            flow = NULL;
        }
    }

    return flow;
}

void brcmf_flowring_detach(struct brcmf_flowring* flow) {
    struct brcmf_bus* bus_if = dev_get_drvdata(flow->dev);
    struct brcmf_pub* drvr = bus_if->drvr;
    struct brcmf_flowring_tdls_entry* search;
    struct brcmf_flowring_tdls_entry* remove;
    uint16_t flowid;

    for (flowid = 0; flowid < flow->nrofrings; flowid++) {
        if (flow->rings[flowid]) {
            brcmf_msgbuf_delete_flowring(drvr, flowid);
        }
    }

    search = flow->tdls_entry;
    while (search) {
        remove = search;
        search = search->next;
        free(remove);
    }
    free(flow->rings);
    free(flow);
}

void brcmf_flowring_configure_addr_mode(struct brcmf_flowring* flow, int ifidx,
                                        enum proto_addr_mode addr_mode) {
    struct brcmf_bus* bus_if = dev_get_drvdata(flow->dev);
    struct brcmf_pub* drvr = bus_if->drvr;
    uint32_t i;
    uint16_t flowid;

    if (flow->addr_mode[ifidx] != addr_mode) {
        for (i = 0; i < ARRAY_SIZE(flow->hash); i++) {
            if (flow->hash[i].ifidx == ifidx) {
                flowid = flow->hash[i].flowid;
                if (flow->rings[flowid]->status != RING_OPEN) {
                    continue;
                }
                flow->rings[flowid]->status = RING_CLOSING;
                brcmf_msgbuf_delete_flowring(drvr, flowid);
            }
        }
        flow->addr_mode[ifidx] = addr_mode;
    }
}

void brcmf_flowring_delete_peer(struct brcmf_flowring* flow, int ifidx, uint8_t peer[ETH_ALEN]) {
    struct brcmf_bus* bus_if = dev_get_drvdata(flow->dev);
    struct brcmf_pub* drvr = bus_if->drvr;
    struct brcmf_flowring_hash* hash;
    struct brcmf_flowring_tdls_entry* prev;
    struct brcmf_flowring_tdls_entry* search;
    uint32_t i;
    uint16_t flowid;
    bool sta;

    sta = (flow->addr_mode[ifidx] == ADDR_INDIRECT);

    search = flow->tdls_entry;
    prev = NULL;
    while (search) {
        if (memcmp(search->mac, peer, ETH_ALEN) == 0) {
            sta = false;
            break;
        }
        prev = search;
        search = search->next;
    }

    hash = flow->hash;
    for (i = 0; i < BRCMF_FLOWRING_HASHSIZE; i++) {
        if ((sta || (memcmp(hash[i].mac, peer, ETH_ALEN) == 0)) && (hash[i].ifidx == ifidx)) {
            flowid = flow->hash[i].flowid;
            if (flow->rings[flowid]->status == RING_OPEN) {
                flow->rings[flowid]->status = RING_CLOSING;
                brcmf_msgbuf_delete_flowring(drvr, flowid);
            }
        }
    }

    if (search) {
        if (prev) {
            prev->next = search->next;
        } else {
            flow->tdls_entry = search->next;
        }
        free(search);
        if (flow->tdls_entry == NULL) {
            flow->tdls_active = false;
        }
    }
}

void brcmf_flowring_add_tdls_peer(struct brcmf_flowring* flow, int ifidx, uint8_t peer[ETH_ALEN]) {
    struct brcmf_flowring_tdls_entry* tdls_entry;
    struct brcmf_flowring_tdls_entry* search;

    tdls_entry = calloc(1, sizeof(*tdls_entry));
    if (tdls_entry == NULL) {
        return;
    }

    memcpy(tdls_entry->mac, peer, ETH_ALEN);
    tdls_entry->next = NULL;
    if (flow->tdls_entry == NULL) {
        flow->tdls_entry = tdls_entry;
    } else {
        search = flow->tdls_entry;
        if (memcmp(search->mac, peer, ETH_ALEN) == 0) {
            goto free_entry;
        }
        while (search->next) {
            search = search->next;
            if (memcmp(search->mac, peer, ETH_ALEN) == 0) {
                goto free_entry;
            }
        }
        search->next = tdls_entry;
    }

    flow->tdls_active = true;
    return;

free_entry:
    free(tdls_entry);
}
