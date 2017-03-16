// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This file contains certain default values used by our stack.

namespace bluetooth {
namespace hci {
namespace defaults {

// 50 ms
constexpr uint16_t kLEConnectionIntervalMin = 0x0028;

// 70 ms
constexpr uint16_t kLEConnectionIntervalMax = 0x0038;

// 60 ms
constexpr uint16_t kLEScanInterval = 0x0060;

// 30 ms
constexpr uint16_t kLEScanWindow = 0x0030;

// 420 ms
constexpr uint16_t kLESupervisionTimeout = 0x002A;

}  // namespace defaults
}  // namespace hci
}  // namespace bluetooth
