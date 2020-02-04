// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_ADDRESS_RANGE_H
#define PLATFORM_ADDRESS_RANGE_H

#include <cstdint>
#include <memory>

namespace magma {

class PlatformAddressRange {
 public:
  static std::unique_ptr<PlatformAddressRange> Create(uint64_t size);

  virtual ~PlatformAddressRange() {}

  virtual uint64_t address() = 0;
  virtual uint64_t size() = 0;
};

}  // namespace magma

#endif  // PLATFORM_ADDRESS_RANGE_H
