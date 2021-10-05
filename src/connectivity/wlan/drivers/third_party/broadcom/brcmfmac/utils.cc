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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <fuchsia/wlan/ieee80211/cpp/fidl.h>

#include <algorithm>
#include <string>
#include <vector>

#include "brcmu_utils.h"
#include "debug.h"
#include "linuxisms.h"
#include "netbuf.h"

namespace wlan_ieee80211 = ::fuchsia::wlan::ieee80211;

#define SSID_PREFIX "<ssid-"
#define SSID_PREFIX_LEN strlen(SSID_PREFIX)
#define SSID_SUFFIX ">"
#define SSID_SUFFIX_LEN strlen(SSID_SUFFIX)

struct brcmf_netbuf* brcmu_pkt_buf_get_netbuf(uint len) {
  struct brcmf_netbuf* netbuf;

  netbuf = brcmf_netbuf_allocate(len);
  if (netbuf) {
    brcmf_netbuf_grow_tail(netbuf, len);
    netbuf->priority = 0;
  }

  return netbuf;
}

/* Free the driver packet. Free the tag if present */
void brcmu_pkt_buf_free_netbuf(struct brcmf_netbuf* netbuf) {
  if (!netbuf) {
    return;
  }

  WARN_ON(brcmf_netbuf_maybe_in_list(netbuf));
  brcmf_netbuf_free(netbuf);
}

/*
 * osl multiple-precedence packet queue
 * hi_prec is always >= the number of the highest non-empty precedence
 */
struct brcmf_netbuf* brcmu_pktq_penq(struct pktq* pq, int prec, struct brcmf_netbuf* p) {
  struct brcmf_netbuf_list* q;

  if (pktq_full(pq) || pktq_pfull(pq, prec)) {
    return NULL;
  }

  q = &pq->q[prec].netbuf_list;
  brcmf_netbuf_list_add_tail(q, p);
  pq->len++;
  pq->enq_cnt++;

  if (pq->hi_prec < prec) {
    pq->hi_prec = (uint8_t)prec;
  }

  return p;
}

struct brcmf_netbuf* brcmu_pktq_penq_head(struct pktq* pq, int prec, struct brcmf_netbuf* p) {
  struct brcmf_netbuf_list* q;

  if (pktq_full(pq) || pktq_pfull(pq, prec)) {
    return NULL;
  }

  q = &pq->q[prec].netbuf_list;
  brcmf_netbuf_list_add_head(q, p);
  pq->len++;
  pq->enq_cnt++;

  if (pq->hi_prec < prec) {
    pq->hi_prec = (uint8_t)prec;
  }

  return p;
}

struct brcmf_netbuf* brcmu_pktq_pdeq(struct pktq* pq, int prec) {
  struct brcmf_netbuf_list* q;
  struct brcmf_netbuf* p;

  q = &pq->q[prec].netbuf_list;
  p = brcmf_netbuf_list_remove_head(q);
  if (p == NULL) {
    return NULL;
  }

  pq->len--;
  return p;
}

/*
 * precedence based dequeue with match function. Passing a NULL pointer
 * for the match function parameter is considered to be a wildcard so
 * any packet on the queue is returned. In that case it is no different
 * from brcmu_pktq_pdeq() above.
 */
struct brcmf_netbuf* brcmu_pktq_pdeq_match(struct pktq* pq, int prec,
                                           bool (*match_fn)(struct brcmf_netbuf* netbuf, void* arg),
                                           void* arg) {
  struct brcmf_netbuf_list* q;
  struct brcmf_netbuf* p;
  struct brcmf_netbuf* next;

  q = &pq->q[prec].netbuf_list;
  brcmf_netbuf_list_for_every_safe(q, p, next) {
    if (match_fn == NULL || match_fn(p, arg)) {
      brcmf_netbuf_list_remove(q, p);
      pq->len--;
      return p;
    }
  }
  return NULL;
}

struct brcmf_netbuf* brcmu_pktq_pdeq_tail(struct pktq* pq, int prec) {
  struct brcmf_netbuf_list* q;
  struct brcmf_netbuf* p;

  q = &pq->q[prec].netbuf_list;
  p = brcmf_netbuf_list_remove_tail(q);
  if (p == NULL) {
    return NULL;
  }

  pq->len--;
  return p;
}

void brcmu_pktq_pflush(struct pktq* pq, int prec, bool dir, bool (*fn)(struct brcmf_netbuf*, void*),
                       void* arg) {
  struct brcmf_netbuf_list* q;
  struct brcmf_netbuf* p;
  struct brcmf_netbuf* next;

  q = &pq->q[prec].netbuf_list;
  brcmf_netbuf_list_for_every_safe(q, p, next) {
    if (fn == NULL || (*fn)(p, arg)) {
      brcmf_netbuf_list_remove(q, p);
      brcmu_pkt_buf_free_netbuf(p);
      pq->len--;
    }
  }
}

void brcmu_pktq_flush(struct pktq* pq, bool dir, bool (*fn)(struct brcmf_netbuf*, void*),
                      void* arg) {
  int prec;
  for (prec = 0; prec < pq->num_prec; prec++) {
    brcmu_pktq_pflush(pq, prec, dir, fn, arg);
  }
}

void brcmu_pktq_init(struct pktq* pq, int num_prec, int max_len) {
  int prec;

  /* pq is variable size; only zero out what's requested */
  memset(pq, 0, offsetof(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

  pq->num_prec = (uint16_t)num_prec;

  pq->max = (uint16_t)max_len;

  for (prec = 0; prec < num_prec; prec++) {
    pq->q[prec].max = pq->max;
    brcmf_netbuf_list_init(&pq->q[prec].netbuf_list);
  }
}

struct brcmf_netbuf* brcmu_pktq_peek_tail(struct pktq* pq, int* prec_out) {
  int prec;

  if (pq->len == 0) {
    return NULL;
  }

  for (prec = 0; prec < pq->hi_prec; prec++)
    if (!brcmf_netbuf_list_is_empty(&pq->q[prec].netbuf_list)) {
      break;
    }

  if (prec_out) {
    *prec_out = prec;
  }

  return brcmf_netbuf_list_peek_tail(&pq->q[prec].netbuf_list);
}

/* Return sum of lengths of a specific set of precedences */
int brcmu_pktq_mlen(struct pktq* pq, uint prec_bmp) {
  int prec, len;

  len = 0;

  for (prec = 0; prec <= pq->hi_prec; prec++)
    if (prec_bmp & (1 << prec)) {
      len += pktq_plen(pq, prec);
    }

  return len;
}

/* Priority dequeue from a specific set of precedences */
struct brcmf_netbuf* brcmu_pktq_mdeq(struct pktq* pq, uint prec_bmp, int* prec_out) {
  struct brcmf_netbuf_list* q;
  struct brcmf_netbuf* p;
  int prec;

  if (pq->len == 0) {
    return NULL;
  }

  while ((prec = pq->hi_prec) > 0 && brcmf_netbuf_list_is_empty(&pq->q[prec].netbuf_list)) {
    pq->hi_prec--;
  }

  while ((prec_bmp & (1 << prec)) == 0 || brcmf_netbuf_list_is_empty(&pq->q[prec].netbuf_list))
    if (prec-- == 0) {
      return NULL;
    }

  q = &pq->q[prec].netbuf_list;
  p = brcmf_netbuf_list_remove_head(q);
  if (p == NULL) {
    return NULL;
  }

  pq->len--;

  if (prec_out) {
    *prec_out = prec;
  }

  return p;
}

/* Produce a human-readable string for boardrev */
char* brcmu_boardrev_str(uint32_t brev, char* buf) {
  char c;

  if (brev < 0x100) {
    snprintf(buf, BRCMU_BOARDREV_LEN, "%d.%d", (brev & 0xf0) >> 4, brev & 0xf);
  } else {
    c = (brev & 0xf000) == 0x1000 ? 'P' : 'A';
    snprintf(buf, BRCMU_BOARDREV_LEN, "%c%03x", c, brev & 0xfff);
  }
  return buf;
}

char* brcmu_dotrev_str(uint32_t dotrev, char* buf) {
  uint8_t dotval[4];

  if (!dotrev) {
    snprintf(buf, BRCMU_DOTREV_LEN, "unknown");
    return buf;
  }
  dotval[0] = (dotrev >> 24) & 0xFF;
  dotval[1] = (dotrev >> 16) & 0xFF;
  dotval[2] = (dotrev >> 8) & 0xFF;
  dotval[3] = dotrev & 0xFF;

  if (dotval[3]) {
    snprintf(buf, BRCMU_DOTREV_LEN, "%d.%d.%d.%d", dotval[0], dotval[1], dotval[2], dotval[3]);
  } else if (dotval[2]) {
    snprintf(buf, BRCMU_DOTREV_LEN, "%d.%d.%d", dotval[0], dotval[1], dotval[2]);
  } else {
    snprintf(buf, BRCMU_DOTREV_LEN, "%d.%d", dotval[0], dotval[1]);
  }

  return buf;
}

void brcmu_set_rx_rate_index_hist_rx11b(const uint32_t (&rx11b)[WSTATS_RATE_RANGE_11B],
                                        uint32_t* out_rx_rate) {
  // Index 0-3: 802.11b
  for (size_t i = 0; i < WSTATS_RATE_RANGE_11B; i++) {
    out_rx_rate[i] = rx11b[i];
  }
}

void brcmu_set_rx_rate_index_hist_rx11g(const uint32_t (&rx11g)[WSTATS_RATE_RANGE_11G],
                                        uint32_t* out_rx_rate) {
  // Index 4-11: 802.11g
  for (size_t i = 0; i < WSTATS_RATE_RANGE_11G; i++) {
    out_rx_rate[i + WSTATS_RATE_RANGE_11B] = rx11g[i];
  }
}

void brcmu_set_rx_rate_index_hist_rx11n(
    const uint32_t (&rx11n)[WSTATS_SGI_RANGE][WSTATS_BW_RANGE_11N][WSTATS_MCS_RANGE_11N],
    uint32_t* out_rx_rate) {
  // Index 12-27: 802.11n 20Mhz, no SGI
  size_t start = WSTATS_RATE_RANGE_11B + WSTATS_RATE_RANGE_11G;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11N; i++) {
    out_rx_rate[i + start] = rx11n[0][0][i];
  }

  // Index 28-43: 802.11n 40Mhz, no SGI
  start += WSTATS_MCS_RANGE_11N;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11N; i++) {
    out_rx_rate[i + start] = rx11n[0][1][i];
  }

  // Index 44-59: 802.11n 20Mhz, SGI
  start += WSTATS_MCS_RANGE_11N;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11N; i++) {
    out_rx_rate[i + start] = rx11n[1][0][i];
  }

  // Index 60-75: 802.11n 20Mhz, SGI
  start += WSTATS_MCS_RANGE_11N;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11N; i++) {
    out_rx_rate[i + start] = rx11n[1][1][i];
  }
}

void brcmu_set_rx_rate_index_hist_rx11ac(
    const uint32_t (
        &rx11ac)[WSTATS_NSS_RANGE][WSTATS_SGI_RANGE][WSTATS_BW_RANGE_11AC][WSTATS_MCS_RANGE_11AC],
    uint32_t* out_rx_rate) {
  // Index 76-85: 802.11ac 20Mhz, no SGI, 1SS
  size_t start = WSTATS_RATE_RANGE_11B + WSTATS_RATE_RANGE_11G +
                 WSTATS_SGI_RANGE * WSTATS_BW_RANGE_11N * WSTATS_MCS_RANGE_11N;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][0][0][i];
  }

  // Index 86-95: 802.11ac 20Mhz, no SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][0][0][i];
  }

  // Index 96-105: 802.11ac 40Mhz, no SGI, 1SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][0][1][i];
  }

  // Index 106-115: 802.11ac 40Mhz, no SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][0][1][i];
  }

  // Index 116-125: 802.11ac 80Mhz, no SGI, 1SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][0][2][i];
  }

  // Index 126-135: 802.11ac 80Mhz, no SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][0][2][i];
  }

  // Index 136-145: 802.11ac 20Mhz, SGI, 1SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][1][0][i];
  }

  // Index 146-155: 802.11ac 20Mhz, SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][1][0][i];
  }

  // Index 156-165: 802.11ac 40Mhz, SGI, 1SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][1][1][i];
  }

  // Index 166-175: 802.11ac 40Mhz, SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][1][1][i];
  }

  // Index 176-185: 802.11ac 80Mhz, SGI, 1SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[0][1][2][i];
  }

  // Index 186-195: 802.11ac 80Mhz, SGI, 2SS
  start += WSTATS_MCS_RANGE_11AC;
  for (size_t i = 0; i < WSTATS_MCS_RANGE_11AC; i++) {
    out_rx_rate[i + start] = rx11ac[1][1][2][i];
  }
}
