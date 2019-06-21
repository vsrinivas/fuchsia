// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <unittest/unittest.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/types.h>

extern "C" zx_handle_t get_root_resource(void);

namespace {

bool bti_create_test() {
    BEGIN_TEST;

    zx::iommu iommu;
    zx::bti bti;
    zx::pmt pmt;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx::unowned_resource root_res(get_root_resource());
    zx_iommu_desc_dummy_t desc;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY,
                              &desc, sizeof(desc), iommu.reset_and_get_address()), ZX_OK);
    ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

    END_TEST;
}

bool bti_pin_test_helper(bool contiguous_vmo) {
    BEGIN_TEST;

    zx::iommu iommu;
    zx::bti bti;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx::unowned_resource root_res(get_root_resource());
    zx_iommu_desc_dummy_t desc;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY,
                              &desc, sizeof(desc), iommu.reset_and_get_address()), ZX_OK);
    ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

    static constexpr uint64_t kPageCount = 256;
    static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
    zx::vmo vmo;
    if (contiguous_vmo) {
        ASSERT_EQ(zx::vmo::create_contiguous(bti, kVmoSize, 0, &vmo), ZX_OK);
    } else {
        ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);
    }

    zx_paddr_t paddrs[kPageCount];
    zx::pmt pmt;
    ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt), ZX_OK);

    ASSERT_EQ(pmt.unpin(), ZX_OK);

    if (contiguous_vmo) {
        for (unsigned i = 1; i < kPageCount; i++) {
            ASSERT_EQ(paddrs[i], paddrs[0] + i * ZX_PAGE_SIZE);
        }
    }

    END_TEST;
}

bool bti_pin_test() {
    return bti_pin_test_helper(false);
}

bool bti_pin_contiguous_test() {
    return bti_pin_test_helper(true);
}

bool bti_pin_contig_flag_test() {
    BEGIN_TEST;

    zx::iommu iommu;
    zx::bti bti;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    zx::unowned_resource root_res(get_root_resource());
    zx_iommu_desc_dummy_t desc;
    // Please do not use get_root_resource() in new code. See ZX-1467.
    ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY,
                              &desc, sizeof(desc), iommu.reset_and_get_address()), ZX_OK);
    ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

    static constexpr uint64_t kPageCount = 256;
    static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create_contiguous(bti, kVmoSize, 0, &vmo), ZX_OK);

    zx_paddr_t paddr;
    zx::pmt pmt;
    ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, 0, kVmoSize, &paddr, 1, &pmt),
              ZX_OK);

    ASSERT_EQ(pmt.unpin(), ZX_OK);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(bti_tests)
RUN_TEST(bti_create_test);
RUN_TEST(bti_pin_test);
RUN_TEST(bti_pin_contiguous_test);
RUN_TEST(bti_pin_contig_flag_test);
END_TEST_CASE(bti_tests)
