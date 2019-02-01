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
#ifndef BRCMFMAC_FLOWRING_H
#define BRCMFMAC_FLOWRING_H

#include "device.h"
#include "netbuf.h"
#include "proto.h"

#define BRCMF_FLOWRING_HASHSIZE 512 /* has to be 2^x */
#define BRCMF_FLOWRING_INVALID_ID 0xFFFFFFFF

struct brcmf_flowring_hash {
    uint8_t mac[ETH_ALEN];
    uint8_t fifo;
    uint8_t ifidx;
    uint16_t flowid;
};

enum ring_status { RING_CLOSED, RING_CLOSING, RING_OPEN };

struct brcmf_flowring_ring {
    uint16_t hash_id;
    bool blocked;
    enum ring_status status;
    struct brcmf_netbuf_list netbuf_list;
};

struct brcmf_flowring_tdls_entry {
    uint8_t mac[ETH_ALEN];
    struct brcmf_flowring_tdls_entry* next;
};

struct brcmf_flowring {
    struct brcmf_device* dev;
    struct brcmf_flowring_hash hash[BRCMF_FLOWRING_HASHSIZE];
    struct brcmf_flowring_ring** rings;
    //spinlock_t block_lock;
    enum proto_addr_mode addr_mode[BRCMF_MAX_IFS];
    uint16_t nrofrings;
    bool tdls_active;
    struct brcmf_flowring_tdls_entry* tdls_entry;
};

uint32_t brcmf_flowring_lookup(struct brcmf_flowring* flow, uint8_t da[ETH_ALEN], uint8_t prio,
                               uint8_t ifidx);
zx_status_t brcmf_flowring_create(struct brcmf_flowring* flow, uint8_t da[ETH_ALEN], uint8_t prio,
                                  uint8_t ifidx);
void brcmf_flowring_delete(struct brcmf_flowring* flow, uint16_t flowid);
void brcmf_flowring_open(struct brcmf_flowring* flow, uint16_t flowid);
uint8_t brcmf_flowring_tid(struct brcmf_flowring* flow, uint16_t flowid);
uint32_t brcmf_flowring_enqueue(struct brcmf_flowring* flow, uint16_t flowid,
                                struct brcmf_netbuf* netbuf);
struct brcmf_netbuf* brcmf_flowring_dequeue(struct brcmf_flowring* flow, uint16_t flowid);
void brcmf_flowring_reinsert(struct brcmf_flowring* flow, uint16_t flowid,
                             struct brcmf_netbuf* netbuf);
uint32_t brcmf_flowring_qlen(struct brcmf_flowring* flow, uint16_t flowid);
uint8_t brcmf_flowring_ifidx_get(struct brcmf_flowring* flow, uint16_t flowid);
struct brcmf_flowring* brcmf_flowring_attach(struct brcmf_device* dev, uint16_t nrofrings);
void brcmf_flowring_detach(struct brcmf_flowring* flow);
void brcmf_flowring_configure_addr_mode(struct brcmf_flowring* flow, int ifidx,
                                        enum proto_addr_mode addr_mode);
void brcmf_flowring_delete_peer(struct brcmf_flowring* flow, int ifidx, uint8_t peer[ETH_ALEN]);
void brcmf_flowring_add_tdls_peer(struct brcmf_flowring* flow, int ifidx, uint8_t peer[ETH_ALEN]);

#endif /* BRCMFMAC_FLOWRING_H */
