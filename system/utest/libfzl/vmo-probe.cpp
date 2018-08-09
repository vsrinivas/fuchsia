// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-probe.h"

#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/rights.h>

namespace vmo_probe {

bool probe_access(void* addr, AccessType access_type, bool expect_can_access) {
    BEGIN_HELPER;
    unittest_printf("probe_access for addr: %lu\n", (size_t)addr);

    switch (access_type) {
    case AccessType::Rd: {
        auto rd_fn = [](void* addr) { g_access_check_var = reinterpret_cast<uint32_t*>(addr)[0]; };
        if (expect_can_access) {
            ASSERT_NO_DEATH(rd_fn, addr, "Read probe failed when it should have succeeded.");
        } else {
            ASSERT_DEATH(rd_fn, addr, "Read probe succeeded when it should have failed.");
        }
    } break;

    case AccessType::Wr: {
        auto wr_fn = [](void* addr) { reinterpret_cast<uint32_t*>(addr)[0] = g_access_check_var; };
        if (expect_can_access) {
            ASSERT_NO_DEATH(wr_fn, addr, "Write probe failed when it should have succeeded.");
        } else {
            ASSERT_DEATH(wr_fn, addr, "Write probe succeeded when it should have failed.");
        }
    } break;
    }

    END_HELPER;
}

bool probe_verify_region(void* start, size_t size, uint32_t access) {
    BEGIN_HELPER;

    auto uint_base = reinterpret_cast<uintptr_t>(start);
    void* probe_points[] = {
        reinterpret_cast<void*>(uint_base),
        reinterpret_cast<void*>(uint_base + (size / 2)),
        reinterpret_cast<void*>(uint_base + size - sizeof(uint32_t)),
    };

    unittest_printf("prove_verify_region for addr: %lu, size: %lu\n", (size_t)start, size);
    for (void* probe_point : probe_points) {
        ASSERT_TRUE(probe_access(probe_point, AccessType::Rd, access & ZX_VM_FLAG_PERM_READ));
        ASSERT_TRUE(probe_access(probe_point, AccessType::Wr, access & ZX_VM_FLAG_PERM_WRITE));
    }

    END_HELPER;
}

} // namespace vmo_probe
