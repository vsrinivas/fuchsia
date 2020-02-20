// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/owned_vmoid.h"

#include <zircon/status.h>
#include <zxtest/zxtest.h>

#include <memory>

#include "storage/buffer/vmoid_registry.h"

namespace storage {
namespace {

const vmoid_t kVmoid = 5;

class MockVmoidRegistry : public storage::VmoidRegistry {
 public:
  bool attached() const { return attached_; }

 private:
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
    EXPECT_FALSE(attached_);
    *out = kVmoid;
    attached_ = true;
    return ZX_OK;
  }

  zx_status_t DetachVmo(vmoid_t vmoid) final {
    EXPECT_EQ(kVmoid, vmoid);
    EXPECT_TRUE(attached_);
    attached_ = false;
    return ZX_OK;
  }

  bool attached_ = false;
};

TEST(OwnedVmoidTest, Uninitialized) {
  MockVmoidRegistry registry;
  OwnedVmoid vmoid(&registry);

  EXPECT_FALSE(vmoid.attached());
}

TEST(OwnedVmoidTest, AttachDetach) {
  MockVmoidRegistry registry;
  OwnedVmoid vmoid(&registry);

  zx::vmo vmo;
  EXPECT_OK(vmoid.AttachVmo(vmo));
  EXPECT_TRUE(vmoid.attached());
  EXPECT_TRUE(registry.attached());
  EXPECT_EQ(vmoid.vmoid(), kVmoid);

  vmoid.Reset();
  EXPECT_FALSE(vmoid.attached());
  EXPECT_FALSE(registry.attached());
}

TEST(OwnedVmoidTest, AutoDetach) {
  MockVmoidRegistry registry;

  {
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_OK(vmoid.AttachVmo(vmo));
  }
  EXPECT_FALSE(registry.attached());
}

TEST(OwnedVmoidTest, Move) {
  MockVmoidRegistry registry;

  {
    // Move before attach
    OwnedVmoid vmoid(&registry);
    OwnedVmoid vmoid2 = std::move(vmoid);
    EXPECT_FALSE(vmoid.attached());
    EXPECT_FALSE(vmoid2.attached());
  }
  {
    // Move after attach
    // Expect the underlying attachment to persist.
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_OK(vmoid.AttachVmo(vmo));
    OwnedVmoid vmoid2 = std::move(vmoid);

    EXPECT_FALSE(vmoid.attached());
    EXPECT_TRUE(vmoid2.attached());
    EXPECT_TRUE(registry.attached());
    EXPECT_EQ(vmoid2.vmoid(), kVmoid);
  }
  {
    // Move after attach/detach
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_OK(vmoid.AttachVmo(vmo));
    vmoid.Reset();
    OwnedVmoid vmoid2 = std::move(vmoid);

    EXPECT_FALSE(vmoid.attached());
    EXPECT_FALSE(vmoid2.attached());
    EXPECT_FALSE(registry.attached());
  }
}

}  // namespace
}  // namespace storage
