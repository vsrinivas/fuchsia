// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"

#include <stdio.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"

// Maximum bytes to dump in hex_dump_str()
#define MAX_DUMP_LEN_IN_A_ROW 16

static char hex_char(uint8_t ch) { return (ch >= 10) ? (ch - 10) + 'a' : ch + '0'; }

static char printable(uint8_t ch) {
  if (ch >= 0x20 && ch < 0x7f) {
    return ch;
  } else {
    return kNP;
  }
}

char* hex_dump_str(char* output, size_t output_size, const void* ptr, size_t len) {
  if (output_size < HEX_DUMP_BUF_SIZE || len > MAX_DUMP_LEN_IN_A_ROW) {
    return NULL;
  }

  const uint8_t* ch = (const uint8_t*)ptr;
  memset(output, ' ', HEX_DUMP_BUF_SIZE);
  for (size_t j = 0; j < MAX_DUMP_LEN_IN_A_ROW && j < len; j++) {
    uint8_t val = ch[j];

    // print the hex part.
    char* hex = &output[j * 3];
    hex[0] = hex_char(val >> 4);
    hex[1] = hex_char(val & 0xf);
    hex[2] = ':';

    // print the ASCII part.
    char* chr = &output[50 + j];
    *chr = printable(val);
  }
  output[HEX_DUMP_BUF_SIZE - 1] = '\0';  // null-terminator

  return output;
}

void hex_dump(const char* prefix, const void* ptr, size_t len) {
  IWL_INFO(NULL, "%sdump %zu (0x%zx) bytes %p:\n", prefix, len, len, ptr);
  if (!ptr) {
    return;
  }

  for (size_t i = 0; i < len; i += MAX_DUMP_LEN_IN_A_ROW) {
    char buf[HEX_DUMP_BUF_SIZE];
    hex_dump_str(buf, sizeof(buf), ptr + i, MIN(len - i, MAX_DUMP_LEN_IN_A_ROW));
    IWL_INFO(NULL, "%s%s\n", prefix, buf);
  }
}
