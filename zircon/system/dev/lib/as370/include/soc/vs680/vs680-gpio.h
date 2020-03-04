// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/algorithm.h>

namespace vs680 {

constexpr uint32_t kGpio1Base = 0xf7e8'0800;
constexpr uint32_t kGpio2Base = 0xf7e8'0c00;
constexpr uint32_t kGpioSize = fbl::round_up<uint32_t, uint32_t>(0x400, PAGE_SIZE);

constexpr uint32_t kPinmuxBase = 0xf7ea'0800;
constexpr uint32_t kPinmuxSize = fbl::round_up<uint32_t, uint32_t>(0x180, PAGE_SIZE);

constexpr uint32_t kGpio1Irq = 78 + 32;
constexpr uint32_t kGpio2Irq = 79 + 32;

}  // namespace vs680
