// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/algorithm.h>

namespace as370 {

constexpr uint32_t kNandBase = 0xf7f1'0000;
constexpr uint32_t kNandSize = fbl::round_up<uint32_t, uint32_t>(0x2084, PAGE_SIZE);

constexpr uint32_t kNandFifoBase = 0xf7f0'0000;
constexpr uint32_t kNandFifoSize = PAGE_SIZE;

}  // namespace as370
