// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

namespace bluetooth {
namespace common {

// Returns a manufacturer name as a string for the given company identifier. If
// |manufacturer_id| does not match a known company then an empty string will be
// returned.
std::string GetManufacturerName(uint16_t manufacturer_id);

}  // namespace common
}  // namespace bluetooth
