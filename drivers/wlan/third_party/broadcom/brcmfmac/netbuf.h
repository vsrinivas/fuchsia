/*
 * Copyright (c) 2018 The Fuchsia Authors
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

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_NETBUF_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_NETBUF_H_

#include <stdlib.h>
#include <sys/types.h>

#include "debug.h"

// TODO(cphoenix): Improve this very partial implementation of netbuf.
// - Appropriate paranoia on function parameters
// - Comments once the API and struct fields are finalized
// - More complete initialization
// - Implement the rest of the functions to replace all the brcmf_netbuf library calls
// - Full renaming of "skb" to "netbuf" in the code
// - Test the implementation

struct brcmf_netbuf;
struct brcmf_netbuf_list {
    uint32_t priority;
    int qlen;
    struct brcmf_netbuf* next;
};

struct brcmf_netbuf {
    uint16_t protocol;
    int priority;
    uint16_t len;
    uint32_t end;
    uint32_t tail;
    uint8_t* data;
    void* next;
    void* prev;
    // Workspace is a small area for use by the driver, on which a driver-specific struct can
    // be superimposed. For example, see fwsignal.c for brcmf_netbuf_workspace.
    // The driver uses it to associate state / information with the packet.
    // Code above and below the driver, and the netbuf library, should not modify this area.
    uint8_t workspace[48];
    uint32_t pkt_type;
    uint32_t ip_summed;
    uint8_t* allocated_buffer;
    uint32_t allocated_size;
    void* eth_header;
};

struct brcmf_netbuf* brcmf_netbuf_allocate(uint32_t size);

static inline uint32_t brcmf_netbuf_head_space(struct brcmf_netbuf* netbuf) {
    return netbuf->data - netbuf->allocated_buffer;
}

static inline uint32_t brcmf_netbuf_tail_space(struct brcmf_netbuf* netbuf) {
    return netbuf->allocated_size - netbuf->len - brcmf_netbuf_head_space(netbuf);
}

static inline void brcmf_netbuf_grow_tail(struct brcmf_netbuf* netbuf, uint32_t len) {
    ZX_DEBUG_ASSERT(netbuf->data + netbuf->len + len <=
                    netbuf->allocated_buffer + netbuf->allocated_size);
    netbuf->len += len;
}

static inline void brcmf_netbuf_shrink_head(struct brcmf_netbuf* netbuf, uint32_t len) {
    ZX_DEBUG_ASSERT(netbuf->len >= len);
    netbuf->data += len;
    netbuf->len -= len;
}

static inline void brcmf_netbuf_list_init(struct brcmf_netbuf_list* head) {
    memset(head, 0, sizeof(*head));
}

void brcmf_netbuf_free(struct brcmf_netbuf* netbuf);

#endif // GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_NETBUF_H_
