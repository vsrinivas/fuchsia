/*
 * Copyright (c) 2016 Broadcom
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

#include "pno.h"

#include <threads.h>
#include <zircon/status.h>

#include "cfg80211.h"
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"

#define BRCMF_PNO_VERSION 2
#define BRCMF_PNO_REPEAT 4
#define BRCMF_PNO_FREQ_EXPO_MAX 3
#define BRCMF_PNO_IMMEDIATE_SCAN_BIT 3
#define BRCMF_PNO_ENABLE_BD_SCAN_BIT 5
#define BRCMF_PNO_ENABLE_ADAPTSCAN_BIT 6
#define BRCMF_PNO_REPORT_SEPARATELY_BIT 11
#define BRCMF_PNO_SCAN_INCOMPLETE 0
#define BRCMF_PNO_WPA_AUTH_ANY 0xFFFFFFFF
#define BRCMF_PNO_HIDDEN_BIT 2
#define BRCMF_PNO_SCHED_SCAN_PERIOD 30

#define BRCMF_PNO_MAX_BUCKETS 16
#define GSCAN_BATCH_NO_THR_SET 101
#define GSCAN_RETRY_THRESHOLD 3

struct brcmf_pno_info {
  int n_reqs;
  struct cfg80211_sched_scan_request* reqs[BRCMF_PNO_MAX_BUCKETS];
  mtx_t req_lock;
};

zx_status_t brcmf_pno_attach(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_pno_info* pi;

  BRCMF_DBG(TRACE, "enter\n");
  pi = static_cast<decltype(pi)>(calloc(1, sizeof(*pi)));
  if (!pi) {
    return ZX_ERR_NO_MEMORY;
  }

  cfg->pno = pi;
  mtx_init(&pi->req_lock, mtx_plain);
  return ZX_OK;
}

void brcmf_pno_detach(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_pno_info* pi;

  BRCMF_DBG(TRACE, "enter\n");
  pi = cfg->pno;
  cfg->pno = NULL;

  WARN_ON(pi->n_reqs);
  mtx_destroy(&pi->req_lock);
  free(pi);
}

uint64_t brcmf_pno_find_reqid_by_bucket(struct brcmf_pno_info* pi, uint32_t bucket) {
  uint64_t reqid = 0;

  mtx_lock(&pi->req_lock);

  if ((int)bucket < pi->n_reqs) {
    reqid = pi->reqs[bucket]->reqid;
  }

  mtx_unlock(&pi->req_lock);
  return reqid;
}

uint32_t brcmf_pno_get_bucket_map(struct brcmf_pno_info* pi, struct brcmf_pno_net_info_le* ni) {
  struct cfg80211_sched_scan_request* req;
  struct cfg80211_match_set* ms;
  uint32_t bucket_map = 0;
  int i, j;

  mtx_lock(&pi->req_lock);
  for (i = 0; i < pi->n_reqs; i++) {
    req = pi->reqs[i];

    if (!req->n_match_sets) {
      continue;
    }
    for (j = 0; j < req->n_match_sets; j++) {
      ms = &req->match_sets[j];
      if (ms->ssid.ssid_len == ni->SSID_len && !memcmp(ms->ssid.ssid, ni->SSID, ni->SSID_len)) {
        bucket_map |= BIT(i);
        break;
      }
      if (is_valid_ether_addr(ms->bssid) && !memcmp(ms->bssid, ni->bssid, ETH_ALEN)) {
        bucket_map |= BIT(i);
        break;
      }
    }
  }
  mtx_unlock(&pi->req_lock);
  return bucket_map;
}
