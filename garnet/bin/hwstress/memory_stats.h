// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/kernel/cpp/fidl.h>
#include <lib/zx/status.h>

namespace hwstress {

// Get current system memory statistics.
zx::status<fuchsia::kernel::MemoryStats> GetMemoryStats();

}  // namespace hwstress
