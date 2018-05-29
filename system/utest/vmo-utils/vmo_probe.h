// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>

namespace vmo_probe {

static volatile uint32_t g_access_check_var;

enum class AccessType { Rd, Wr };

// Test read or write access at the address |addr|.
bool probe_access(void* addr, AccessType access_type, bool expect_can_access);

// Tests read or write access over a region starting at |start| and ending at
// |start| + |size|.
bool probe_verify_region(void* start, size_t size, uint32_t access);

} // namespace vmo_probe
