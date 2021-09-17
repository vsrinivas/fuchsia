// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/debug.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <numeric>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/driver-inspector.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

// Maximum bytes to dump in hex_dump_str()
constexpr size_t kMaxDumpLenInARow = 16;

zx_status_t iwl_debug_core_dump(struct device* dev, const char* core_dump_name,
                                const char** core_dumps, size_t* core_dump_sizes,
                                size_t core_dump_count) {
  if (core_dump_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  } else if (core_dump_count == 1) {
    return dev->inspector->PublishCoreDump(core_dump_name, {core_dumps[0], core_dump_sizes[0]});
  } else {
    const size_t total_size =
        std::accumulate(core_dump_sizes, core_dump_sizes + core_dump_count, 0);
    auto buffer = std::make_unique<char[]>(total_size);
    size_t offset = 0;
    for (size_t i = 0; i < core_dump_count; ++i) {
      std::memcpy(buffer.get() + offset, core_dumps[i], core_dump_sizes[i]);
      offset += core_dump_sizes[i];
    }
    return dev->inspector->PublishCoreDump(core_dump_name, {buffer.get(), total_size});
  }
}

static char hex_char(uint8_t ch) { return (ch >= 10) ? (ch - 10) + 'a' : ch + '0'; }

static char printable(uint8_t ch) {
  if (ch >= 0x20 && ch < 0x7f) {
    return ch;
  } else {
    return kNP;
  }
}

char* hex_dump_str(char* output, size_t output_size, const void* ptr, size_t len) {
  if (output_size < HEX_DUMP_BUF_SIZE || len > kMaxDumpLenInARow) {
    return nullptr;
  }

  const uint8_t* ch = reinterpret_cast<const uint8_t*>(ptr);
  memset(output, ' ', HEX_DUMP_BUF_SIZE);
  for (size_t j = 0; j < kMaxDumpLenInARow && j < len; j++) {
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
  zxlogf(INFO, "%sdump %zu (0x%zx) bytes %p:\n", prefix, len, len, ptr);
  if (!ptr) {
    return;
  }

  for (size_t i = 0; i < len; i += kMaxDumpLenInARow) {
    char buf[HEX_DUMP_BUF_SIZE];
    hex_dump_str(buf, sizeof(buf), reinterpret_cast<const uint8_t*>(ptr) + i,
                 std::min(len - i, kMaxDumpLenInARow));
    zxlogf(INFO, "%s%s\n", prefix, buf);
  }
}
