// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/msi.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/iommu.h>

#include <zxtest/zxtest.h>

namespace {

extern "C" zx_handle_t get_root_resource(void);

class RootResourceFixture : public zxtest::Test {
 public:
  void SetUp() override {
    zx_iommu_desc_dummy_t desc = {};
    root_resource_ = zx::unowned_resource(get_root_resource());
    ASSERT_OK(
        zx::iommu::create(*root_resource_, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu_));
    ASSERT_OK(zx::bti::create(iommu_, 0, 0xdeadbeef, &bti_));
  }

  bool MsiTestsSupported() {
    zx::msi msi;
    return !(zx::msi::allocate(*root_resource_, 1, &msi) == ZX_ERR_NOT_SUPPORTED);
  }

 protected:
  zx::unowned_resource root_resource_;
  zx::iommu iommu_;
  zx::bti bti_;
};

}  // namespace
