// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "magma_util/macros.h"
#include "platform_address_range.h"

namespace magma {

class ZirconPlatformAddressRange : public PlatformAddressRange {
 public:
  ~ZirconPlatformAddressRange() override {}

  ZirconPlatformAddressRange(zx::vmar vmar, uint64_t address, uint64_t size)
      : vmar_(std::move(vmar)), address_(address), size_(size) {}

  uint64_t address() override { return address_; }
  uint64_t size() override { return size_; }

 private:
  zx::vmar vmar_;
  uint64_t address_;
  uint64_t size_;
};

std::unique_ptr<PlatformAddressRange> PlatformAddressRange::Create(uint64_t size) {
  uint64_t address;
  zx::vmar vmar;
  zx_status_t status = zx::vmar::root_self()->allocate(0, size, 0, &vmar, &address);
  if (status != ZX_OK)
    return DRETP(nullptr, "zx_vmar_allocate failed: %x", status);
  return std::make_unique<ZirconPlatformAddressRange>(std::move(vmar), address, size);
}

}  // namespace magma
