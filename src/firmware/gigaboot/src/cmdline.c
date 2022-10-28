// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmdline.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utf_conversion.h>
#include <xefi.h>

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
      printf("WARNING: Not enough space to store cmdline\n");
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
    if (!(*ptr) || isspace(*ptr)) {
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

// Get any load options from the image and append them to the boot arguments.
void cmdline_append_load_options(void) {
  size_t args_len = 0;

  efi_status status;
  size_t options_len;
  size_t options_len_codepoints;
  void* options;
  uint8_t* args;
  uint8_t* ptr;

  status = xefi_get_load_options(&options_len, &options);

  if (status != EFI_SUCCESS) {
    printf("xefi_get_load_options failed: %zu\n", status);
    return;
  }

  if (options_len > 0) {
    // To ensure we allocate enough space for arbitrary UTF-8 representations of
    // strings we receive as UTF-16, we need to allocate a *larger* buffer than
    // the UTF-16 string, since in the worst case each codepoint could require 3
    // bytes to express as UTF-8.  For codepoints beyond the BMP which would
    // require 4 bytes as UTF-8, UTF-16 must express them as surrogate pairs, so
    // 3x is sufficient.
    options_len_codepoints = options_len / sizeof(char16_t);
    args_len = options_len_codepoints * 3;
    status = gBS->AllocatePool(EfiLoaderData, args_len, (void**)&args);
    if (status != EFI_SUCCESS) {
      printf("allocating arg memory failed: %zu\n", status);
      goto alloc_fail;
    }

    ptr = args;
    zx_status_t result;
    size_t converted_args_len = args_len;

    result = utf16_to_utf8(options, options_len_codepoints, args, &converted_args_len);
    if (result != ZX_OK) {
      printf("Could not convert options from UTF16->UTF8: %d\n", result);
      goto fail;
    }

    if (converted_args_len > args_len) {
      printf("Insufficient space to convert options from UTF16->UTF8: have %zu, want %zu\n",
             args_len, converted_args_len);
      goto fail;
    }

    // Skip first argument which is the filename.
    ptr = args;
    while (converted_args_len > 0 && *ptr != ' ') {
      ptr++;
      converted_args_len--;
    }
    while (converted_args_len > 0 && *ptr == ' ') {
      ptr++;
      converted_args_len--;
    }

    cmdline_append((char*)ptr, converted_args_len);

  fail:
    gBS->FreePool(args);
  }

alloc_fail:
  gBS->FreePool(options);
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
  return (uint32_t)atol(val);
}

void cmdline_clear(void) {
  buffer_next = 0;
  entry_count = 0;
}
