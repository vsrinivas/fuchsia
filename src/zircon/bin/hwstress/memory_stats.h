// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_HWSTRESS_MEMORY_STATS_H_
#define SRC_ZIRCON_BIN_HWSTRESS_MEMORY_STATS_H_

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/zx/result.h>

namespace hwstress {

// Get current system memory statistics.
zx::result<fuchsia::kernel::MemoryStats> GetMemoryStats();

}  // namespace hwstress

#endif  // SRC_ZIRCON_BIN_HWSTRESS_MEMORY_STATS_H_
