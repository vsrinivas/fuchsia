// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/fit/defer.h>
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
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, zx_system_get_page_size()));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, zx_system_get_page_size());

  // zero a few words in the middle of the page.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 42, 91, NULL, 0));

  EXPECT_TRUE(AllSameVal(ptr, 42, 0xff));
  EXPECT_TRUE(AllSameVal(ptr + 42, 91, 0));
  EXPECT_TRUE(AllSameVal(ptr + 42 + 91, zx_system_get_page_size() - 42 - 91, 0xff));
}

TEST(VmoZeroTestCase, UnalignedCommitted) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, zx_system_get_page_size() * 2));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, zx_system_get_page_size() * 2);

  // zero across both page boundaries
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size() / 2, zx_system_get_page_size(),
                         NULL, 0));

  EXPECT_TRUE(AllSameVal(ptr, zx_system_get_page_size() / 2, 0xff));
  EXPECT_TRUE(AllSameVal(ptr + zx_system_get_page_size() / 2, zx_system_get_page_size(), 0));
  EXPECT_TRUE(AllSameVal(ptr + zx_system_get_page_size() + zx_system_get_page_size() / 2,
                         zx_system_get_page_size() / 2, 0xff));
}

TEST(VmoZeroTestCase, UnalignedUnCommitted) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &vmo));

  EXPECT_EQ(0, VmoCommittedBytes(vmo));

  // zero across both page boundaries. As these are already known zero pages this should not reuslt
  // in any pages being committed.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size() / 2, zx_system_get_page_size(),
                         NULL, 0));

  EXPECT_EQ(0, VmoCommittedBytes(vmo));
}

TEST(VmoZeroTestCase, DecommitMiddle) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 3, 0, &vmo));

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, zx_system_get_page_size() * 3));
  uint8_t *ptr = mapping.bytes();

  memset(ptr, 0xff, zx_system_get_page_size() * 3);
  EXPECT_EQ(zx_system_get_page_size() * 3, VmoCommittedBytes(vmo));

  // zero across all three pages. This should decommit the middle one.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size() / 2,
                         zx_system_get_page_size() * 2, NULL, 0));

  // Only two pages should be committed
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(vmo));
}

TEST(VmoZeroTestCase, Contiguous) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  zx::unowned_resource root_res(get_root_resource());
  zx::iommu iommu;
  zx::bti bti;
  auto final_bti_check = vmo_test::CreateDeferredBtiCheck(bti);

  zx_iommu_desc_dummy_t desc;
  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));
  bti = vmo_test::CreateNamedBti(iommu, 0, 0xdeadbeef, "VmoZero Contiguous");

  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create_contiguous(bti, zx_system_get_page_size() * 2, 0, &vmo));
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(vmo));

  // Pin momentarily to retrieve the physical address
  zx_paddr_t phys_addr;
  {
    zx::pmt pmt;
    EXPECT_OK(bti.pin(ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, vmo, 0, zx_system_get_page_size() * 2,
                      &phys_addr, 1, &pmt));
    pmt.unpin();
  }

  Mapping mapping;
  EXPECT_OK(mapping.Init(vmo, zx_system_get_page_size() * 2));
  uint8_t *ptr = mapping.bytes();
  memset(ptr, 0xff, zx_system_get_page_size() * 2);

  // Zero a page. should not cause decommit as our VMO must remain contiguous.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(vmo));

  EXPECT_TRUE(AllSameVal(ptr, zx_system_get_page_size(), 0));
  EXPECT_TRUE(AllSameVal(ptr + zx_system_get_page_size(), zx_system_get_page_size(), 0xff));

  // Pin again to make sure physical contiguity was preserved.
  zx_paddr_t phys_addr2;
  {
    zx::pmt pmt;
    EXPECT_OK(bti.pin(ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, vmo, 0, zx_system_get_page_size() * 2,
                      &phys_addr2, 1, &pmt));
    pmt.unpin();
  }
  EXPECT_EQ(phys_addr, phys_addr2);
}

TEST(VmoZeroTestCase, ContentInParentAndChild) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  // Create a child of both pages, and then just fork the first 1
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &child));
  VmoWrite(child, 2, 0);

  // As page 2 is still CoW with the parent page 1 cannot be decommitted as it would then see old
  // parent data.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));

  VmoCheck(child, 0, 0);
}

TEST(VmoZeroTestCase, EmptyCowChildren) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  // Commit the first page by writing to it.
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Validate child contents.
  VmoCheck(child, 1, 0);

  // Zero the child. Should not change pages committed, but child should now read as 0.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  VmoCheck(child, 0, 0);
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Now zero the parent. Should be no need to keep the underlying pages around, dropping committed.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  VmoCheck(parent, 0, 0);
  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, MergeZeroChildren) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size(), &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Zero the parent. Pages should move to the child.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child));

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
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &child));

  // Validate initial state.
  VmoCheck(child, 1, 0);
  VmoCheck(child, 2, zx_system_get_page_size());
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child));

  // Zero first page of the child. This doesn't change number of pages committed as our sibling
  // is still using it.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child));

  // Close the parent to make the merge happen.
  parent.reset();

  // Should only have 1 page attributed to us, and reading should still give us our expected pages
  // and not those of our merge partner.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child));
  VmoCheck(child, 0, 0);
  VmoCheck(child, 2, zx_system_get_page_size());
}

// Similar to AllocateAfterMerge, but by merging with a hidden child we will hit the non fast_merge
// path in the kernel.
TEST(VmoZeroTestCase, AllocateAfterMergeHiddenChild) {
  zx::vmo parent;
  InitPageTaggedVmo(3, &parent);

  zx::vmo child1, child2;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 3, &child1));
  EXPECT_EQ(zx_system_get_page_size() * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child1));

  // Zero a page in the parent before creating the next child. This places a zero page in the
  // common hidden parent.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  EXPECT_EQ(zx_system_get_page_size() * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child1));

  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 3, &child2));

  // Zero the middle page of child1. This leaves the number of comitted pages the same.
  EXPECT_OK(child1.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size(), zx_system_get_page_size(),
                            NULL, 0));
  EXPECT_EQ(zx_system_get_page_size() * 3,
            VmoCommittedBytes(parent) + VmoCommittedBytes(child1) + VmoCommittedBytes(child2));

  // Validate page states.
  VmoCheck(child2, 0, 0);
  VmoCheck(child2, 2, zx_system_get_page_size());
  VmoCheck(child2, 3, zx_system_get_page_size() * 2);
  EXPECT_EQ(zx_system_get_page_size() * 3,
            VmoCommittedBytes(parent) + VmoCommittedBytes(child1) + VmoCommittedBytes(child2));

  // Close the first child, forcing that hidden parent to merge with the hidden parent of parent and
  // child2. Child1's zero page should be discarded and not overwrite the forked version, and the
  // page we zeroed in the parent should also not get overridden.
  VmoCheck(child1, 1, 0);
  VmoCheck(child1, 0, zx_system_get_page_size());
  VmoCheck(child1, 3, zx_system_get_page_size() * 2);
  child1.reset();

  VmoCheck(parent, 0, 0);
  VmoCheck(parent, 2, zx_system_get_page_size());
  VmoCheck(parent, 3, zx_system_get_page_size() * 2);
  VmoCheck(child2, 0, 0);
  VmoCheck(child2, 2, zx_system_get_page_size());
  VmoCheck(child2, 3, zx_system_get_page_size() * 2);
  EXPECT_EQ(zx_system_get_page_size() * 2, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));

  // Write to a different byte in our zero page to see if we can uncover child1's data.
  VmoWrite(parent, 1, 64);
  VmoCheck(parent, 0, 0);
  EXPECT_EQ(zx_system_get_page_size() * 3, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));

  // Fork the middle page that child1 zeroed and ensure we CoW the correct underlying page.
  VmoWrite(child2, 5, zx_system_get_page_size() + 64);
  VmoCheck(child2, 2, zx_system_get_page_size());
  VmoCheck(parent, 0, zx_system_get_page_size() + 64);
  VmoCheck(parent, 2, zx_system_get_page_size());
  EXPECT_EQ(zx_system_get_page_size() * 4, VmoCommittedBytes(parent) + VmoCommittedBytes(child2));
}

TEST(VmoZeroTestCase, WriteCowParent) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Write to the parent to perform a COW copy.
  VmoCheck(parent, 1, 0);
  VmoWrite(parent, 2, 0);

  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child));

  // Zero the child. This should decommit the child page.
  VmoCheck(child, 1, 0);
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));
  VmoCheck(child, 0, 0);
  VmoCheck(parent, 2, 0);
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Close the parent. No pages should get merged.
  parent.reset();
  VmoCheck(child, 0, 0);
  EXPECT_EQ(0, VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, ChildZeroThenWrite) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  zx::vmo child;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 2, &child));

  // Parent should have the page currently attributed to it.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));

  // Page attribution should be unchanged.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child));

  // Write to the child, should cause a new page allocation.
  VmoWrite(child, 1, 0);

  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child));

  // Reset the parent. The two committed pages should be different, and the parents page should be
  // dropped.
  parent.reset();
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child));
}

TEST(VmoZeroTestCase, Nested) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 2, 0, &parent));
  VmoWrite(parent, 1, 0);

  // Create two children.
  zx::vmo child1, child2;
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size(), &child1));
  EXPECT_OK(parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size(), &child2));

  // Should have 1 page total attributed to the parent.
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(parent));
  EXPECT_EQ(0, VmoCommittedBytes(child1));
  EXPECT_EQ(0, VmoCommittedBytes(child2));

  // Zero the parent, this will cause the page to have to get forked down the intermediate hidden
  // nodes.
  EXPECT_OK(parent.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size(), NULL, 0));

  EXPECT_EQ(0, VmoCommittedBytes(parent));
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child1));
  EXPECT_EQ(zx_system_get_page_size(), VmoCommittedBytes(child2));
}

TEST(VmoZeroTestCase, ZeroLengths) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));

  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 0, 0, NULL, 0));
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, 10, 0, NULL, 0));
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size(), 0, NULL, 0));
}

// Test that we handle free pages correctly when both decomitting and allocating new pages in a
// single zero operation.
TEST(VmoZeroTestcase, ZeroFreesAndAllocates) {
  zx::vmo parent;
  EXPECT_OK(zx::vmo::create(zx_system_get_page_size() * 3, 0, &parent));

  // Commit the second page with non-zero data so that we have to fork it later.
  VmoWrite(parent, 1, zx_system_get_page_size());

  // Create two levels of children so we are forced to fork a page when inserting a marker later.
  zx::vmo intermediate;
  EXPECT_OK(
      parent.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 3, &intermediate));
  zx::vmo child;
  EXPECT_OK(
      intermediate.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 3, &child));

  // Commit the first page in the child so we have something to decommit later.
  VmoWrite(child, 1, 0);

  // Now zero the child. The first page gets decommitted, and potentially used to fulfill the page
  // allocation involved in forking the second page into the intermediate.
  EXPECT_OK(child.op_range(ZX_VMO_OP_ZERO, 0, zx_system_get_page_size() * 2, NULL, 0));
}

// Tests that if a hidden parent ends up with markers then when its children perform resize
// operations markers that are still visible to the sibling are not removed from the parent.
TEST(VmoZeroTestCase, ResizeOverHiddenMarkers) {
  zx::vmo vmo;

  ASSERT_OK(zx::vmo::create(zx_system_get_page_size() * 4, ZX_VMO_RESIZABLE, &vmo));

  // Commit the second last page with non-zero data so we can place a marker over it in a child
  // later.
  VmoWrite(vmo, 1, zx_system_get_page_size() * 2);

  // Create an intermediate hidden parent, this ensures that when the child is resized the pages in
  // the range cannot simply be freed, as there is still a child of the root that needs them.
  zx::vmo intermediate;
  ASSERT_OK(
      vmo.create_child(ZX_VMO_CHILD_SNAPSHOT, 0, zx_system_get_page_size() * 4, &intermediate));

  // Now zero that second last page slot. As our parent has a page here a marker has to get inserted
  // to prevent seeing back to the parent. We explicitly do not zero the first or last page as in
  // those cases the parent limits could be updated instead.
  ASSERT_OK(vmo.op_range(ZX_VMO_OP_ZERO, zx_system_get_page_size() * 2, zx_system_get_page_size(),
                         nullptr, 0));

  // Create a sibling over this zero page.
  zx::vmo sibling;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SNAPSHOT, zx_system_get_page_size() * 2,
                             zx_system_get_page_size(), &sibling));

  // The sibling should see the zeros.
  ASSERT_NO_FATAL_FAILURES(VmoCheck(sibling, 0, 0));

  // Finally resize the VMO such that only our sibling sees the range in the parent that contains
  // that zero marker. In doing this resize the marker should not be freed.
  ASSERT_OK(vmo.set_size(zx_system_get_page_size()));

  // Check that the sibling still correctly sees zero.
  ASSERT_NO_FATAL_FAILURES(VmoCheck(sibling, 0, 0));

  // Writing to the sibling should commit a fresh zero page due to the marker, and should not
  // attempt to refork the page from the root.
  VmoWrite(sibling, 1, 0);
}

}  // namespace vmo_test
