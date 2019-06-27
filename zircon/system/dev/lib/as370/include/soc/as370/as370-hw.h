// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

namespace as370 {

// GBL Global Control Registers.
constexpr uint32_t kGlobalBase = 0xf7ea'0000;
constexpr uint32_t kGlobalSize = fbl::round_up<uint32_t, uint32_t>(0x2'0000, PAGE_SIZE);

// AIO 64b dHub Registers (first 0x1'0000 is TCM).
constexpr uint32_t kAudioDhubBase = 0xf740'0000;
constexpr uint32_t kAudioDhubSize = fbl::round_up<uint32_t, uint32_t>(0x2'0000, PAGE_SIZE);

// AVIO Global Registers.
constexpr uint32_t kAudioGlobalBase = 0xf742'0000;
constexpr uint32_t kAudioGlobalSize = fbl::round_up<uint32_t, uint32_t>(0x2'0000, PAGE_SIZE);

// AIO I2S Registers.
constexpr uint32_t kAudioI2sBase = 0xf744'0000;
constexpr uint32_t kAudioI2sSize = fbl::round_up<uint32_t, uint32_t>(0x2'0000, PAGE_SIZE);

} // namespace as370
