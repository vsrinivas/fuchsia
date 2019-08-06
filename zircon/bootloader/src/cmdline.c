// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osboot.h"

#define CMDLINE_MAX_ITEMS 128
#define CMDLINE_MAX_STRINGDATA (PAGE_SIZE * 3)

static char buffer[CMDLINE_MAX_STRINGDATA];
static size_t buffer_next = 0;

typedef struct {
  char* key;
  char* val;
  size_t klen;
  size_t vlen;
} kv_t;

static kv_t entry[CMDLINE_MAX_ITEMS];
static size_t entry_count;

size_t cmdline_to_string(char* ptr, size_t max) {
  char* start = ptr;

  if (max == 0) {
    return 0;
  }

  for (size_t n = 0; n < entry_count; n++) {
    if ((entry[n].klen + entry[n].vlen + 3) > max) {
      // require space for: space + key + equal + value + null
      break;
    }
    if (n > 0) {
      *ptr++ = ' ';
      max--;
    }
    memcpy(ptr, entry[n].key, entry[n].klen);
    ptr += entry[n].klen;
    max -= entry[n].klen;
    if (entry[n].vlen) {
      *ptr++ = '=';
      max--;
      memcpy(ptr, entry[n].val, entry[n].vlen);
      ptr += entry[n].vlen;
      max -= entry[n].vlen;
    }
  }
  *ptr++ = 0;
  return ptr - start;
}

static void entry_add(const char* key, size_t klen, const char* val, size_t vlen) {
  if (klen == 0) {
    // empty keys are not allowed
    return;
  }

  if ((klen > 1024) || (vlen > 1024)) {
    // huge keys and values are not allowed
    return;
  }

  if ((sizeof(buffer) - buffer_next) < (klen + vlen + 2)) {
    // give up if it won't fit
    return;
  }

  size_t n;
  for (n = 0; n < entry_count; n++) {
    if ((entry[n].klen == klen) && !memcmp(key, entry[n].key, klen)) {
      goto write_value;
    }
  }
  if (n == CMDLINE_MAX_ITEMS) {
    // no space in table
    return;
  }

  // new entry
  entry_count++;
  entry[n].key = buffer + buffer_next;
  entry[n].klen = klen;
  memcpy(entry[n].key, key, klen);
  entry[n].key[klen] = 0;
  buffer_next += klen + 1;

write_value:
  entry[n].val = buffer + buffer_next;
  entry[n].vlen = vlen;
  memcpy(entry[n].val, val, vlen);
  entry[n].val[vlen] = 0;
  buffer_next += vlen + 1;
}

void cmdline_set(const char* key, const char* val) {
  entry_add(key, strlen(key), val, strlen(val));
}

void cmdline_append(const char* ptr, size_t len) {
  const char* key;
  const char* val;

restart:
  while (len > 0) {
    if (isspace(*ptr)) {
      ptr++;
      len--;
      continue;
    }
    key = ptr;
    while (len > 0) {
      if (*ptr == '=') {
        size_t klen = ptr - key;
        ptr++;
        len--;
        val = ptr;
        while ((len > 0) && !isspace(*ptr)) {
          len--;
          ptr++;
        }
        size_t vlen = ptr - val;
        entry_add(key, klen, val, vlen);
        goto restart;
      }
      if (isspace(*ptr)) {
        break;
      }
      ptr++;
      len--;
    }
    size_t klen = ptr - key;
    entry_add(key, klen, NULL, 0);
  }
}

const char* cmdline_get(const char* key, const char* _default) {
  size_t klen = strlen(key);
  for (size_t n = 0; n < entry_count; n++) {
    if ((entry[n].klen == klen) && !memcmp(key, entry[n].key, klen)) {
      return entry[n].val;
    }
  }
  return _default;
}

uint32_t cmdline_get_uint32(const char* key, uint32_t _default) {
  const char* val = cmdline_get(key, NULL);
  if (val == NULL) {
    return _default;
  }
  return atol(val);
}
