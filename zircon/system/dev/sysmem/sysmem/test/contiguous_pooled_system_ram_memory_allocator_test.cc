// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/platform-defs.h>
#include <lib/fake-bti/bti.h>
#include <lib/zx/vmar.h>
#include <zxtest/zxtest.h>

#include <vector>

#include "contiguous_pooled_system_ram_memory_allocator.h"

class FakeOwner : public MemoryAllocator::Owner {
public:
    FakeOwner() {
        EXPECT_OK(fake_bti_create(bti_.reset_and_get_address()));
    }

    ~FakeOwner() {
        fake_bti_destroy(bti_.release());
    }

    const zx::bti& bti() override {
        return bti_;
    }
    zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) override {
        return zx::vmo::create(size, 0u, vmo_out);
    }

private:
    zx::bti bti_;
};

namespace {
TEST(ContiguousPooled, Full) {
    FakeOwner owner;
    constexpr uint32_t kVmoSize = 4096;
    constexpr uint32_t kVmoCount = 1024;
    ContiguousPooledSystemRamMemoryAllocator allocator(&owner, kVmoSize * kVmoCount);

    EXPECT_OK(allocator.Init());

    std::vector<zx::vmo> vmos;
    for (uint32_t i = 0; i < kVmoCount; ++i) {
        zx::vmo vmo;
        EXPECT_OK(allocator.Allocate(kVmoSize, &vmo));
        vmos.push_back(std::move(vmo));
    }

    zx::vmo vmo;
    EXPECT_NOT_OK(allocator.Allocate(kVmoSize, &vmo));

    uintptr_t ptr;
    EXPECT_OK(zx::vmar::root_self()->map(0u, vmos[0], 0u, kVmoSize, ZX_VM_PERM_READ, &ptr));

    vmos[0].reset();

    // The mapping should prevent the allocator from marking the memory as free.
    EXPECT_NOT_OK(allocator.Allocate(kVmoSize, &vmo));
    EXPECT_OK(zx::vmar::root_self()->unmap(ptr, kVmoSize));

    EXPECT_OK(allocator.Allocate(kVmoSize, &vmo));

    // Destroy half of all vmos.
    for (uint32_t i = 0; i < kVmoCount; i += 2) {
        vmos[i].reset();
    }

    // There shouldn't be enough contiguous address space for even 1 extra byte.
    // This check relies on sequential Allocate() calls to a brand-new allocator
    // being laid out sequentially, so isn't a fundamental check - if the
    // allocator's layout strategy changes this check might start to fail
    // without there necessarily being a real problem.
    EXPECT_NOT_OK(allocator.Allocate(kVmoSize + 1, &vmo));
}

} // namespace
