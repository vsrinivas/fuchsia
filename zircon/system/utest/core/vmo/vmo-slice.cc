// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/syscalls/iommu.h>

#include <zxtest/zxtest.h>

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace {

TEST(VmoSliceTestCase, WriteThrough) {
  // Create parent VMO with 4 pages.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 4, 0, &vmo));

  // Write to our first two pages.
  uint32_t val = 42;
  EXPECT_OK(vmo.write(&val, 0, sizeof(val)));
  EXPECT_OK(vmo.write(&val, PAGE_SIZE, sizeof(val)));

  // Create a child that can see the middle two pages.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, PAGE_SIZE, PAGE_SIZE * 2, &slice_vmo));

  // The first page in the slice should have the contents we wrote to the parent earlier.
  EXPECT_OK(slice_vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 42);

  // Write to the two pages in the slice. The second page is the third page in the parent and
  // was never written to or allocated previously. After this the parent should contain
  // [42, 84, 84, unallocated]
  val = 84;
  EXPECT_OK(slice_vmo.write(&val, 0, sizeof(val)));
  EXPECT_OK(slice_vmo.write(&val, PAGE_SIZE, sizeof(val)));

  EXPECT_OK(vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 42);
  EXPECT_OK(vmo.read(&val, PAGE_SIZE, sizeof(val)));
  EXPECT_EQ(val, 84);
  EXPECT_OK(vmo.read(&val, PAGE_SIZE * 2, sizeof(val)));
  EXPECT_EQ(val, 84);
  EXPECT_OK(vmo.read(&val, PAGE_SIZE * 3, sizeof(val)));
  EXPECT_EQ(val, 0);
}

TEST(VmoSliceTestCase, DecommitParent) {
  // Create parent VMO and put some data in it.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  uint8_t val = 42;
  EXPECT_OK(vmo.write(&val, 0, sizeof(val)));

  // Create the child and check we can see what we wrote in the parent.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &slice_vmo));

  EXPECT_OK(slice_vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 42);

  // Decommit from the parent should cause the slice to see fresh zero pages.
  EXPECT_OK(vmo.op_range(ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, nullptr, 0));

  EXPECT_OK(slice_vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 0);
}

TEST(VmoSliceTestCase, Nested) {
  // Create parent.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &vmo));

  // Put something in the first page.
  uint32_t val = 42;
  EXPECT_OK(vmo.write(&val, 0, sizeof(val)));

  // Create a child that can see both pages.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE * 2, &slice_vmo));

  // Create a child of the child.
  zx::vmo slice_slice_vmo;
  ASSERT_OK(slice_vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE * 2, &slice_slice_vmo));

  // Check the child of the child sees parent data.
  EXPECT_OK(slice_slice_vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 42);

  // Write to child of child and check parent updates.
  val = 84;
  EXPECT_OK(slice_slice_vmo.write(&val, 0, sizeof(val)));
  EXPECT_OK(slice_slice_vmo.write(&val, PAGE_SIZE, sizeof(val)));

  EXPECT_OK(vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 84);
  EXPECT_OK(vmo.read(&val, PAGE_SIZE, sizeof(val)));
  EXPECT_EQ(val, 84);
}

TEST(VmoSliceTestCase, NonSlice) {
  // Create parent.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE * 2, ZX_VMO_RESIZABLE, &vmo));

  // Creating children that are not strict slices should fail.
  zx::vmo slice_vmo;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE * 3, &slice_vmo));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            vmo.create_child(ZX_VMO_CHILD_SLICE, PAGE_SIZE, PAGE_SIZE * 2, &slice_vmo));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            vmo.create_child(ZX_VMO_CHILD_SLICE, PAGE_SIZE * 2, PAGE_SIZE, &slice_vmo));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, vmo.create_child(ZX_VMO_CHILD_SLICE, 0, UINT64_MAX, &slice_vmo));
  const uint64_t nearly_int_max = UINT64_MAX - PAGE_SIZE + 1;
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
            vmo.create_child(ZX_VMO_CHILD_SLICE, 0, nearly_int_max, &slice_vmo));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            vmo.create_child(ZX_VMO_CHILD_SLICE, nearly_int_max, PAGE_SIZE, &slice_vmo));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
            vmo.create_child(ZX_VMO_CHILD_SLICE, nearly_int_max, nearly_int_max, &slice_vmo));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
            vmo.create_child(ZX_VMO_CHILD_SLICE, nearly_int_max, UINT64_MAX, &slice_vmo));
}

TEST(VmoSliceTestCase, NonResizable) {
  // Create a resizable parent.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo));

  // Any slice creation should fail.
  zx::vmo slice_vmo;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &slice_vmo));
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo.create_child(ZX_VMO_CHILD_SLICE | ZX_VMO_CHILD_RESIZABLE, 0,
                                                  PAGE_SIZE, &slice_vmo));

  // Switch to a correctly non-resizable parent.
  vmo.reset();
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // A resizable slice should fail.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo.create_child(ZX_VMO_CHILD_SLICE | ZX_VMO_CHILD_RESIZABLE, 0,
                                                  PAGE_SIZE, &slice_vmo));
}

TEST(VmoSliceTestCase, CommitChild) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Create a child and commit it.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &slice_vmo));
  EXPECT_OK(slice_vmo.op_range(ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, nullptr, 0));

  // Now write to the child and verify the parent reads the same.
  uint8_t val = 42;
  EXPECT_OK(slice_vmo.write(&val, 0, sizeof(val)));
  EXPECT_OK(vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 42);
}

TEST(VmoSliceTestCase, DecommitChild) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Write to the parent to commit some pages.
  uint8_t val = 42;
  EXPECT_OK(vmo.write(&val, 0, sizeof(val)));

  // Create a child and decommit.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &slice_vmo));
  EXPECT_OK(slice_vmo.op_range(ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, nullptr, 0));

  // Reading from the parent should result in fresh zeros.
  // Now write to the child and verify the parent reads the same.
  EXPECT_OK(vmo.read(&val, 0, sizeof(val)));
  EXPECT_EQ(val, 0);
}

TEST(VmoSliceTestCase, ZeroSized) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Create some zero sized children.
  zx::vmo slice_vmo1;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, 0, &slice_vmo1));
  zx::vmo slice_vmo2;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, PAGE_SIZE, 0, &slice_vmo2));

  // Reading and writing should fail.
  uint8_t val = 42;
  EXPECT_EQ(slice_vmo1.read(&val, 0, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(slice_vmo2.read(&val, 0, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(slice_vmo1.write(&val, 0, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_EQ(slice_vmo2.write(&val, 0, 1), ZX_ERR_OUT_OF_RANGE);
}

TEST(VmoSliceTestCase, ChildSliceOfContiguousParentIsContiguous) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }
  const size_t size = PAGE_SIZE;

  zx::vmo parent_contig_vmo;
  zx::unowned_resource root_res(get_root_resource());

  zx::iommu iommu;
  zx::bti bti;

  zx_iommu_desc_dummy_t desc;
  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));

  EXPECT_OK(zx::bti::create(iommu, 0, 0xdeadbeef, &bti));

  EXPECT_OK(zx::vmo::create_contiguous(bti, size, 0, &parent_contig_vmo));

  // Create child slice.
  zx::vmo child;
  ASSERT_OK(parent_contig_vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &child));

  zx_info_vmo_t info;
  ASSERT_OK(child.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.flags & ZX_INFO_VMO_CONTIGUOUS, ZX_INFO_VMO_CONTIGUOUS);
}

TEST(VmoSliceTestCase, ZeroChildren) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Currently the parent has no children, so ZX_VMO_ZERO_CHILDREN should be set.
  zx_signals_t pending;
  ASSERT_OK(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, ZX_VMO_ZERO_CHILDREN);

  // Create child slice.
  zx::vmo child;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &child));

  // Currently the parent has one child, so ZX_VMO_ZERO_CHILDREN should be
  // cleared.  Since child VMO creation is synchronous, this signal must already
  // be clear.
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, 0);

  // Close child slice.
  child.reset();

  // Closing the child doesn't strictly guarantee that ZX_VMO_ZERO_CHILDREN is set
  // immediately, but it should be set very soon if not already.
  ASSERT_OK(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, ZX_VMO_ZERO_CHILDREN);
}

TEST(VmoSliceTestCase, ZeroChildrenGrandchildClosedLast) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Currently the parent has no children, so ZX_VMO_ZERO_CHILDREN should be set.
  zx_signals_t pending;
  ASSERT_OK(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, ZX_VMO_ZERO_CHILDREN);

  // Create child slice.
  zx::vmo child;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &child));

  // Currently the parent has one child, so ZX_VMO_ZERO_CHILDREN should be
  // cleared.  Since child VMO creation is synchronous, this signal must already
  // be clear.
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, 0);

  // Create grandchild slice.
  zx::vmo grandchild;
  ASSERT_OK(child.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &grandchild));

  // Currently the parent has one child and one grandchild, so ZX_VMO_ZERO_CHILDREN should be
  // cleared.
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, 0);

  // Close child slice.  Leave grandchild alone.
  child.reset();

  // Currently the parent has one grandchild, so ZX_VMO_ZERO_CHILDREN should be
  // cleared.
  ASSERT_EQ(ZX_ERR_TIMED_OUT,
            vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, 0);

  // Close grandchild slice.
  grandchild.reset();

  // Closing the grandchild (last of all direct or indirect children) doesn't strictly guarantee
  // that ZX_VMO_ZERO_CHILDREN is set immediately, but it should be set very soon if not already.
  ASSERT_OK(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite(), &pending));
  ASSERT_EQ(pending & ZX_VMO_ZERO_CHILDREN, ZX_VMO_ZERO_CHILDREN);
}

TEST(VmoSliceTestCase, CowPageSourceThroughSlices) {
  // Create parent VMO.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  // Commit the page so it becomes the initial content for future children.
  ASSERT_OK(vmo.op_range(ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, nullptr, 0));

  // Create a COW child so that we have a hidden parent as the root page source.
  zx::vmo cow_child;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &cow_child));

  // Now create a slice of the cow_child
  zx::vmo slice;
  ASSERT_OK(cow_child.create_child(ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &slice));

  // Now create a cow child of the slice.
  // Currently this is forbidden and returns ZX_ERR_NOT_SUPPORTED. If it didn't the
  // cow_child2.write would cause a kernel assertion to trigger. Once bug 36841 is fixed the else
  // branch can be removed.
  zx::vmo cow_child2;
  zx_status_t result = slice.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &cow_child2);
  if (result == ZX_OK) {
    // Attempt to write to this child. This will require propagating page through both hidden and
    // non hidden VMOs.
    uint8_t val;
    EXPECT_OK(cow_child2.write(&val, 0, 1));
  } else {
    EXPECT_EQ(result, ZX_ERR_NOT_SUPPORTED);
  }
}

TEST(VmoSliceTestCase, RoundUpSizePhysical) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }
  const size_t size = PAGE_SIZE;

  zx::vmo parent_contig_vmo;
  zx::unowned_resource root_res(get_root_resource());

  zx::iommu iommu;
  zx::bti bti;

  zx_iommu_desc_dummy_t desc;
  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));

  EXPECT_OK(zx::bti::create(iommu, 0, 0xdeadbeef, &bti));

  EXPECT_OK(zx::vmo::create_contiguous(bti, size, 0, &parent_contig_vmo));

  // Create child slice with size < PAGE_SIZE, should round up and succeed.
  zx::vmo child;
  ASSERT_OK(parent_contig_vmo.create_child(ZX_VMO_CHILD_SLICE, 0, 42, &child));
}

TEST(VmoSliceTestCase, RoundUpSize) {
  // Create parent VMO and put some data in it near the end.
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));

  uint8_t val = 42;
  EXPECT_OK(vmo.write(&val, PAGE_SIZE - 64, sizeof(val)));

  // Create child slice with size < PAGE_SIZE, should round up and succeed.
  zx::vmo slice_vmo;
  ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, 42, &slice_vmo));

  // Should be able to read the data in the rounded up portion.
  EXPECT_OK(slice_vmo.read(&val, PAGE_SIZE - 64, sizeof(val)));
  EXPECT_EQ(val, 42);
}

}  // namespace
