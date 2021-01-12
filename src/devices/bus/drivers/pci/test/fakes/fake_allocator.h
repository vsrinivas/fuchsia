// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_ALLOCATOR_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_ALLOCATOR_H_

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <optional>

#include "../../allocation.h"

namespace pci {

// Normally we would track the allocations and assert on issues during
// cleanup, but presently with an IsolatedDevmgr we don't have a way
// to cleanly tear down the FakeBusDriver, so no dtors on anything
// will be called anyway.
class FakeAllocation : public PciAllocation {
 public:
  FakeAllocation(std::optional<zx_paddr_t> base, size_t size)
      : PciAllocation(zx::resource(ZX_HANDLE_INVALID)),
        base_((base.has_value()) ? *base : 0),
        size_(size) {
    zxlogf(INFO, "fake allocation created [%#lx, %#lx)", base_, base_ + size);
  }
  zx_paddr_t base() const final { return base_; }
  size_t size() const final { return size_; }
  zx_status_t CreateVmObject(zx::vmo* out_vmo) const final {
    return zx::vmo::create(size_, 0, out_vmo);
  }

 private:
  zx_paddr_t base_;
  size_t size_;
};

class FakeAllocator : public PciAllocator {
 public:
  zx::status<std::unique_ptr<PciAllocation>> Allocate(std::optional<zx_paddr_t> base,
                                                      size_t size) final {
    auto allocation = std::unique_ptr<PciAllocation>(new FakeAllocation(base, size));
    return zx::ok(std::move(allocation));
  }

  zx_status_t SetParentAllocation(std::unique_ptr<PciAllocation> alloc) final {
    (void)alloc.release();
    return ZX_OK;
  }
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_ALLOCATOR_H_
