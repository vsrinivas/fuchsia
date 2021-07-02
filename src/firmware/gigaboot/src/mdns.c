// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mdns.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device_id.h"
#include "fastboot.h"
#include "inet6.h"
#include "netifc.h"

#define MDNS_FLAG_QUERY_RESPONSE 0x8000
#define MDNS_FLAG_AUTHORITATIVE 0x400

#define MDNS_CLASS_IN 1
#define MDNS_CLASS_CACHE_FLUSH (1 << 15)

#define MDNS_SHORT_TTL (2 * 60)

#define MDNS_PORT (5353)

#define MDNS_BROADCAST_FREQ_MS (10000)  // Broadcast every 10 seconds.
static char device_nodename[DEVICE_ID_MAX];

static struct mdns_name_segment fastboot_name_segments[3] = {
    {
        .name = "_fastboot",
        .loc = 0,
        .next = &fastboot_name_segments[1],
    },
    {
        .name = "_udp",
        .loc = 0,
        .next = &fastboot_name_segments[2],
    },
    {
        .name = "local",
        .loc = 0,
        .next = NULL,
    },
};

/*** packet writing ***/
bool mdns_write_bytes(struct mdns_buf* b, const void* bytes, size_t len) {
  if ((b->used + len) > MDNS_MAX_PKT) {
    printf("%s: len=%zu is too big, already used %zu bytes.\n", __func__, len, b->used);
    return false;
  }
  memcpy(&b->data[b->used], bytes, len);
  b->used += len;
  return true;
}

bool mdns_write_u16(struct mdns_buf* b, uint16_t v) {
  v = htons(v);
  return mdns_write_bytes(b, &v, sizeof(v));
}

bool mdns_write_u32(struct mdns_buf* b, uint32_t v) {
  v = htonl(v);
  return mdns_write_bytes(b, &v, sizeof(v));
}

bool mdns_write_name(struct mdns_buf* b, struct mdns_name_segment* name) {
  for (struct mdns_name_segment* cur = name; cur != NULL; cur = cur->next) {
    if (cur->loc) {
      if (!mdns_write_u16(b, cur->loc | MDNS_NAME_AT_OFFSET_FLAG))
        return false;
      return true;
    }

    uint16_t start = (uint16_t)b->used;
    size_t len = strlen(cur->name);
    if (len > UINT8_MAX) {
      return false;
    }
    uint8_t data = (uint8_t)len;
    if (!mdns_write_bytes(b, &data, sizeof(data))) {
      return false;
    }
    if (!mdns_write_bytes(b, cur->name, len)) {
      return false;
    }
    cur->loc = start;
  }

  uint8_t data = 0;
  return mdns_write_bytes(b, &data, sizeof(data));
}

static bool mdns_write_ptr(struct mdns_buf* b, struct mdns_ptr_record* p) {
  return mdns_write_name(b, p->name);
}
static bool mdns_write_aaaa(struct mdns_buf* b, struct mdns_aaaa_record* a) {
  return mdns_write_bytes(b, a->addr.x, IP6_ADDR_LEN);
}

static bool mdns_write_srv(struct mdns_buf* b, struct mdns_srv_record* s) {
  if (!mdns_write_u16(b, s->priority))
    return false;
  if (!mdns_write_u16(b, s->weight))
    return false;
  if (!mdns_write_u16(b, s->port))
    return false;
  if (!mdns_write_name(b, s->target))
    return false;
  return true;
}

bool mdns_write_record(struct mdns_buf* b, struct mdns_record* r) {
  if (!mdns_write_name(b, r->name))
    return false;
  if (!mdns_write_u16(b, r->type))
    return false;
  if (!mdns_write_u16(b, r->record_class))
    return false;
  if (!mdns_write_u32(b, r->time_to_live))
    return false;

  // Reserve some space for the data length.
  size_t data_loc = b->used;
  if (!mdns_write_u16(b, 0))
    return false;

  bool ret = false;
  switch (r->type) {
    case MDNS_TYPE_PTR:
      ret = mdns_write_ptr(b, &r->data.ptr);
      break;
    case MDNS_TYPE_AAAA:
      ret = mdns_write_aaaa(b, &r->data.aaaa);
      break;
    case MDNS_TYPE_SRV:
      ret = mdns_write_srv(b, &r->data.srv);
      break;
    default:
      printf("mdns bad type!\n");
      return false;
  }

  if (!ret)
    return false;

  // Calculate data length written for record.
  uint16_t data_len = (uint16_t)(b->used - data_loc);
  // Don't count the two bytes where we're storing the data length.
  data_len -= sizeof(data_len);
  data_len = htons(data_len);
  memcpy(&b->data[data_loc], &data_len, sizeof(data_len));
  return true;
}

bool mdns_write_packet(struct mdns_header* hdr, struct mdns_record* records, struct mdns_buf* pkt) {
  memset(pkt, 0, sizeof(*pkt));
  bool ok = mdns_write_u16(pkt, hdr->id) && mdns_write_u16(pkt, hdr->flags) &&
            mdns_write_u16(pkt, hdr->question_count) && mdns_write_u16(pkt, hdr->answer_count) &&
            mdns_write_u16(pkt, hdr->authority_count) && mdns_write_u16(pkt, hdr->additional_count);
  if (!ok)
    return false;

  uint32_t record_count =
      hdr->question_count + hdr->answer_count + hdr->authority_count + hdr->additional_count;
  for (uint32_t i = 0; i < record_count; i++) {
    if (!mdns_write_record(pkt, &records[i]))
      return false;
  }
  return true;
}

static struct mdns_buf pkt;
bool mdns_send(struct mdns_header* hdr, struct mdns_record* records) {
  if (!mdns_write_packet(hdr, records, &pkt))
    return false;
  if (udp6_send(pkt.data, pkt.used, &ip6_mdns_broadcast, MDNS_PORT, MDNS_PORT))
    return false;
  return true;
}

/*** fastboot mdns broadcasts ***/
bool mdns_broadcast_fastboot(bool finished) {
  // Clear name segment locations.
  fastboot_name_segments[0].loc = 0;
  fastboot_name_segments[1].loc = 0;
  fastboot_name_segments[2].loc = 0;
  struct mdns_name_segment ptr_name = {
      .name = device_nodename,
      .loc = 0,
      .next = fastboot_name_segments,
  };
  struct mdns_name_segment my_name = {
      .name = device_nodename,
      .loc = 0,
      .next = &fastboot_name_segments[2],
  };

  uint16_t ttl = (finished ? 0 : MDNS_SHORT_TTL);

  // MDNS query response.
  struct mdns_header hdr = {
      .id = 0,
      .flags = MDNS_FLAG_QUERY_RESPONSE | MDNS_FLAG_AUTHORITATIVE,
      .question_count = 0,
      .answer_count = 1,
      .authority_count = 0,
      .additional_count = 2,
  };
  // MDNS response records.
  struct mdns_record records[] = {
      {
          .name = fastboot_name_segments,
          .type = MDNS_TYPE_PTR,
          .record_class = MDNS_CLASS_CACHE_FLUSH | MDNS_CLASS_IN,
          .time_to_live = ttl,
          .data.ptr.name = &ptr_name,
      },
      {
          .name = &ptr_name,
          .type = MDNS_TYPE_SRV,
          .record_class = MDNS_CLASS_CACHE_FLUSH | MDNS_CLASS_IN,
          .time_to_live = ttl,
          .data.srv =
              {
                  .priority = 0,
                  .weight = 0,
                  .port = FB_SERVER_PORT,
                  .target = &my_name,
              },
      },
      {
          .name = &my_name,
          .type = MDNS_TYPE_AAAA,
          .record_class = MDNS_CLASS_CACHE_FLUSH | MDNS_CLASS_IN,
          .time_to_live = ttl,
          .data.aaaa.addr = ll_ip6_addr,
      },
  };

  return mdns_send(&hdr, records);
}

static int mdns_active = 0;
void mdns_start(uint32_t namegen) {
  device_id(ll_mac_addr, device_nodename, namegen);
  printf("mdns: starting broadcast\n");
  netifc_set_timer(MDNS_BROADCAST_FREQ_MS);
  mdns_broadcast_fastboot(false);
  mdns_active = 1;
}

void mdns_poll(void) {
  if (!mdns_active)
    return;
  if (netifc_timer_expired()) {
    mdns_broadcast_fastboot(false);
    netifc_set_timer(MDNS_BROADCAST_FREQ_MS);
  }
}

void mdns_stop(void) {
  if (!mdns_active)
    return;

  mdns_active = 0;
  mdns_broadcast_fastboot(true);
}
