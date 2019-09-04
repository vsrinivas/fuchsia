// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

namespace as370 {

constexpr uint32_t kThermalBase = 0xf7ea'0800;
constexpr uint32_t kThermalSize = fbl::round_up<uint32_t, uint32_t>(0x1c, PAGE_SIZE);

}  // namespace as370
