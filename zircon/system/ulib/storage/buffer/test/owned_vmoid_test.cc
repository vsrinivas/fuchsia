// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/owned_vmoid.h"

#include <zircon/status.h>
#include <gtest/gtest.h>

#include <memory>

#include "storage/buffer/vmoid_registry.h"

namespace storage {
namespace {

const vmoid_t kVmoid = 5;

class MockVmoidRegistry : public storage::VmoidRegistry {
 public:
  bool attached() const { return attached_; }

 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final {
    EXPECT_FALSE(attached_);
    *out = storage::Vmoid(kVmoid);
    attached_ = true;
    return ZX_OK;
  }

  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final {
    EXPECT_EQ(kVmoid, vmoid.TakeId());
    EXPECT_TRUE(attached_);
    attached_ = false;
    return ZX_OK;
  }

  bool attached_ = false;
};

TEST(OwnedVmoidTest, Uninitialized) {
  MockVmoidRegistry registry;
  OwnedVmoid vmoid(&registry);

  EXPECT_FALSE(vmoid.IsAttached());
}

TEST(OwnedVmoidTest, AttachDetach) {
  MockVmoidRegistry registry;
  OwnedVmoid vmoid(&registry);

  zx::vmo vmo;
  EXPECT_EQ(vmoid.AttachVmo(vmo), ZX_OK);
  EXPECT_TRUE(vmoid.IsAttached());
  EXPECT_TRUE(registry.attached());
  EXPECT_EQ(vmoid.get(), kVmoid);

  vmoid.Reset();
  EXPECT_FALSE(vmoid.IsAttached());
  EXPECT_FALSE(registry.attached());
}

TEST(OwnedVmoidTest, AutoDetach) {
  MockVmoidRegistry registry;

  {
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_EQ(vmoid.AttachVmo(vmo), ZX_OK);
  }
  EXPECT_FALSE(registry.attached());
}

TEST(OwnedVmoidTest, Move) {
  MockVmoidRegistry registry;

  {
    // Move before attach
    OwnedVmoid vmoid(&registry);
    OwnedVmoid vmoid2 = std::move(vmoid);
    EXPECT_FALSE(vmoid.IsAttached());
    EXPECT_FALSE(vmoid2.IsAttached());
  }
  {
    // Move after attach
    // Expect the underlying attachment to persist.
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_EQ(vmoid.AttachVmo(vmo), ZX_OK);
    OwnedVmoid vmoid2 = std::move(vmoid);

    EXPECT_FALSE(vmoid.IsAttached());
    EXPECT_TRUE(vmoid2.IsAttached());
    EXPECT_TRUE(registry.attached());
    EXPECT_EQ(vmoid2.get(), kVmoid);
  }
  {
    // Move after attach/detach
    OwnedVmoid vmoid(&registry);
    zx::vmo vmo;
    ASSERT_EQ(vmoid.AttachVmo(vmo), ZX_OK);
    vmoid.Reset();
    OwnedVmoid vmoid2 = std::move(vmoid);

    EXPECT_FALSE(vmoid.IsAttached());
    EXPECT_FALSE(vmoid2.IsAttached());
    EXPECT_FALSE(registry.attached());
  }
}

}  // namespace
}  // namespace storage
