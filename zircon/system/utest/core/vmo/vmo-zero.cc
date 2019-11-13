// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/syscalls/iommu.h>

#include <zxtest/zxtest.h>

#include "helpers.h"

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace vmo_test {

bool AllSameVal(uint8_t *ptr, size_t len, uint8_t val) {
  for (size_t i = 0; i < len; i++) {
    if (ptr[i] != val) {
      return false;
    }
  }
  return true;
}

TEST(VmoZeroTestCase, UnalignedSubPage) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, PAGE_SIZE));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, PAGE_SIZE);

  // zero a few words in the middle of the page.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 42, 91, NULL, 0));

  EXPECT_TRUE(AllSameVal(ptr, 42, 0xff));
  EXPECT_TRUE(AllSameVal(ptr + 42, 91, 0));
  EXPECT_TRUE(AllSameVal(ptr + 42 + 91, PAGE_SIZE - 42 - 91, 0xff));
}

TEST(VmoZeroTestCase, UnalignedCommitted) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, PAGE_SIZE * 2));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, PAGE_SIZE * 2);

  // zero across both page boundaries
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, PAGE_SIZE / 2, PAGE_SIZE, NULL, 0));

  EXPECT_TRUE(AllSameVal(ptr, PAGE_SIZE / 2, 0xff));
  EXPECT_TRUE(AllSameVal(ptr + PAGE_SIZE / 2, PAGE_SIZE, 0));
  EXPECT_TRUE(AllSameVal(ptr + PAGE_SIZE + PAGE_SIZE / 2, PAGE_SIZE / 2, 0xff));
}

TEST(VmoZeroTestCase, UnalignedUnCommitted) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &vmo));

  EXPECT_EQ(0, VmoCommittedBytes(vmo));

  // zero across both page boundaries. As these are already known zero pages this should not reuslt
  // in any pages being committed.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, PAGE_SIZE / 2, PAGE_SIZE, NULL, 0));

  EXPECT_EQ(0, VmoCommittedBytes(vmo));
}

TEST(VmoZeroTestCase, DecommitMiddle) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 3, 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, PAGE_SIZE * 3));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, PAGE_SIZE * 3);
  EXPECT_EQ(PAGE_SIZE * 3, VmoCommittedBytes(vmo));

  // zero across all three pages. This should decommit the middle one.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, PAGE_SIZE / 2, PAGE_SIZE * 2, NULL, 0));

  // Only two pages should be committed
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(vmo));
}

TEST(VmoZeroTestCase, Contiguous) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  zx::unowned_resource root_res(get_root_resource());
  zx::iommu iommu;
  zx::bti bti;

  zx_iommu_desc_dummy_t desc;
  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));

  EXPECT_OK(zx::bti::create(iommu, 0, 0xdeadbeef, &bti));

  zx::vmo vmo;

  EXPECT_OK(zx::vmo::create_contiguous(bti, PAGE_SIZE * 2, 0, &vmo));
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(vmo));

  // Pin momentarily to retrieve the physical address
  zx_paddr_t phys_addr;
  {
    zx::pmt pmt;
    EXPECT_OK(
        bti.pin(ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, vmo, 0, PAGE_SIZE * 2, &phys_addr, 1, &pmt));
    pmt.unpin();
  }

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, PAGE_SIZE * 2));
  uint8_t *ptr = mapping.bytes();
  memset(ptr, 0xff, PAGE_SIZE * 2);

  // Zero a page. should not cause decommit as our VMO must remain contiguous.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(vmo));

  EXPECT_TRUE(AllSameVal(ptr, PAGE_SIZE, 0));
  EXPECT_TRUE(AllSameVal(ptr + PAGE_SIZE, PAGE_SIZE, 0xff));

  // Pin again to make sure physical contiguity was preserved.
  zx_paddr_t phys_addr2;
  {
    zx::pmt pmt;
    EXPECT_OK(bti.pin(ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, vmo, 0, PAGE_SIZE * 2, &phys_addr2, 1,
                      &pmt));
    pmt.unpin();
  }
  EXPECT_EQ(phys_addr, phys_addr2);
}

TEST(VmoZeroTestCase, EmptyCowChildren) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &parent));
  // Commit the first page by writing to it.
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Validate child contents.
  VmoCheck(child, 1, 0);

  // Zero the child. Should not change pages committed, but child should now read as 0.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  VmoCheck(child, 0, 0);
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Now zero the parent. Should be no need to keep the underlying pages around, dropping committed.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  VmoCheck(parent, 0, 0);
  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, MergeZeroChildren) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &parent));
  EXPECT_OK(parent.op_range(ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, NULL, 0));

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Zero the parent. Pages should move to the child.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child));

  // Close the child. Pages should cease being committed and not move to the parent.
  child.reset();
  EXPECT_EQ(0, VmoCommittedBytes(parent));
}

// Tests that after merging a child with its hidden parent that hidden pages are correctly preserved
// and do not get replaced by hidden parents pages.
TEST(VmoZeroTestCase, AllocateAfterMerge) {
  zx::vmo parent;
  InitPageTaggedVmo(2, &parent);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 2, &child));

  // Validate initial state.
  VmoCheck(child, 1, 0);
  VmoCheck(child, 2, PAGE_SIZE);
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child));

  // Zero first page of the child. This doesn't change number of pages committed as our sibling
  // is still using it.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child));

  // Close the parent to make the merge happen.
  parent.reset();

  // Should only have 1 page attributed to us, and reading should still give us our expected pages
  // and not those of our merge partner.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child));
  VmoCheck(child, 0, 0);
  VmoCheck(child, 2, PAGE_SIZE);
}

// Similar to AllocateAfterMerge, but by merging with a hidden child we will hit the non fast_merge
// path in the kernel.
TEST(VmoZeroTestCase, AllocateAfterMergeHiddenChild) {
  zx::vmo parent;
  InitPageTaggedVmo(3, &parent);

  zx::vmo child1, child2;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 3, &child1));
  EXPECT_EQ(PAGE_SIZE * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child1));

  // Zero a page in the parent before creating the next child. This places a zero page in the
  // common hidden parent.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(PAGE_SIZE * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child1));

  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 3, &child2));

  // Zero the middle page of child1. This leaves the number of comitted pages the same.
  EXPECT_OK(child1.op_range(ZX_VMO_OP_ZERO, PAGE_SIZE, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(PAGE_SIZE * 3,
            VmoCommittedBytes(parent) + VmoCommittedBytes(child1) + VmoCommittedBytes(child2));

  // Validate page states.
  VmoCheck(child2, 0, 0);
  VmoCheck(child2, 2, PAGE_SIZE);
  VmoCheck(child2, 3, PAGE_SIZE * 2);
  EXPECT_EQ(PAGE_SIZE * 3,
            VmoCommittedBytes(parent) + VmoCommittedBytes(child1) + VmoCommittedBytes(child2));

  // Close the first child, forcing that hidden parent to merge with the hidden parent of parent and
  // child2. Child1's zero page should be discarded and not overwrite the forked version, and the
  // page we zeroed in the parent should also not get overridden.
  VmoCheck(child1, 1, 0);
  VmoCheck(child1, 0, PAGE_SIZE);
  VmoCheck(child1, 3, PAGE_SIZE * 2);
  child1.reset();

  VmoCheck(parent, 0, 0);
  VmoCheck(parent, 2, PAGE_SIZE);
  VmoCheck(parent, 3, PAGE_SIZE * 2);
  VmoCheck(child2, 0, 0);
  VmoCheck(child2, 2, PAGE_SIZE);
  VmoCheck(child2, 3, PAGE_SIZE * 2);
  EXPECT_EQ(PAGE_SIZE * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));

  // Write to a different byte in our zero page to see if we can uncover child1's data.
  VmoWrite(parent, 1, 64);
  VmoCheck(parent, 0, 0);
  EXPECT_EQ(PAGE_SIZE * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));

  // Fork the middle page that child1 zeroed and ensure we CoW the correct underlying page.
  VmoWrite(child2, 5, PAGE_SIZE + 64);
  VmoCheck(child2, 2, PAGE_SIZE);
  VmoCheck(parent, 0, PAGE_SIZE + 64);
  VmoCheck(parent, 2, PAGE_SIZE);
  EXPECT_EQ(PAGE_SIZE * 4, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));
}

TEST(VmoZeroTestCase, WriteCowParent) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Write to the parent to perform a COW copy.
  VmoCheck(parent, 1, 0);
  VmoWrite(parent, 2, 0);

  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child));

  // Zero the child. This should decommit the child page.
  VmoCheck(child, 1, 0);
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));
  VmoCheck(child, 0, 0);
  VmoCheck(parent, 2, 0);
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Close the parent. No pages should get merged.
  parent.reset();
  VmoCheck(child, 0, 0);
  EXPECT_EQ(0, VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, ChildZeroThenWrite) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));

  // Page attribution should be unchanged.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Write to the child, should cause a new page allocation.
  VmoWrite(child, 1, 0);

  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child));

  // Reset the parent. The two committed pages should be different, and the parents page should be
  // dropped.
  parent.reset();
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, Nested) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &parent));
  EXPECT_OK(parent.op_range(ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, NULL, 0));

  // Create two children.
  zx::vmo child1, child2;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &child1));
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &child2));

  // Should have 1 page total attributed to the parent.
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child1));
  EXPECT_EQ(0, VmoCommittedBytes(child2));

  // Zero the parent, this will cause the page to have to get forked down the intermediate hidden
  // nodes.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, PAGE_SIZE, NULL, 0));

  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child1));
  EXPECT_EQ(PAGE_SIZE, VmoCommittedBytes(child2));
}

}  // namespace vmo_test
