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
