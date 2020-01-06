// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <lib/fzl/memory-probe.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

namespace {

TEST(VmoCloneTestCase, SizeAlign) {
  zx_handle_t vmo;
  zx_status_t status = zx_vmo_create(0, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // create clones with different sizes, make sure the created size is a multiple of a page size
  for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
    zx_handle_t clone_vmo;
    EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, s, &clone_vmo), "vm_clone");

    // should be the size rounded up to the nearest page boundary
    uint64_t size = 0x99999999;
    zx_status_t status = zx_vmo_get_size(clone_vmo, &size);
    EXPECT_OK(status, "vm_object_get_size");
    EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");

    // close the handle
    EXPECT_OK(zx_handle_close(clone_vmo), "handle_close");
  }

  // close the handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

// Tests that a vmo's name propagates to its child.
TEST(VmoCloneTestCase, NameProperty) {
  zx_handle_t vmo;
  zx_handle_t clone_vmo[2];

  // create a vmo
  const size_t size = PAGE_SIZE * 4;
  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "vm_object_create");
  EXPECT_OK(zx_object_set_property(vmo, ZX_PROP_NAME, "test1", 5), "zx_object_set_property");

  // clone it
  clone_vmo[0] = ZX_HANDLE_INVALID;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[0]),
            "vm_clone");
  EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[0], "vm_clone_handle");
  char name[ZX_MAX_NAME_LEN];
  EXPECT_OK(zx_object_get_property(clone_vmo[0], ZX_PROP_NAME, name, ZX_MAX_NAME_LEN),
            "zx_object_get_property");
  EXPECT_TRUE(!strcmp(name, "test1"), "get_name");

  // clone it a second time w/o the rights property
  EXPECT_OK(zx_handle_replace(vmo, ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHTS_PROPERTY, &vmo));
  clone_vmo[1] = ZX_HANDLE_INVALID;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo[1]),
            "vm_clone");
  EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo[1], "vm_clone_handle");
  EXPECT_OK(zx_object_get_property(clone_vmo[0], ZX_PROP_NAME, name, ZX_MAX_NAME_LEN),
            "zx_object_get_property");
  EXPECT_TRUE(!strcmp(name, "test1"), "get_name");

  // close the original handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");

  // close the clone handles
  for (auto h : clone_vmo)
    EXPECT_OK(zx_handle_close(h), "handle_close");
}

// verify that the parent is visible through decommitted pages
TEST(VmoCloneTestCase, Decommit) {
  zx_handle_t vmo;
  zx_handle_t clone_vmo;

  // create a vmo
  const size_t size = PAGE_SIZE * 4;
  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "vm_object_create");

  // clone it and map that
  clone_vmo = ZX_HANDLE_INVALID;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
  EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");

  // decommit is not supported on clones or plain vmos which have children
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
            zx_vmo_op_range(clone_vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, NULL, 0));
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, NULL, 0));

  // close the clone handle
  EXPECT_OK(zx_handle_close(clone_vmo), "handle_close");

  // Once the clone is closed, decommit should work
  EXPECT_OK(zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, NULL, 0));

  // close the original handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

// verify the affect of commit on a clone
TEST(VmoCloneTestCase, Commit) {
  zx_handle_t vmo;
  zx_handle_t clone_vmo;
  uintptr_t ptr;
  uintptr_t clone_ptr;
  volatile uint32_t *p;
  volatile uint32_t *cp;

  // create a vmo
  const size_t size = PAGE_SIZE * 4;
  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "vm_object_create");

  // map it
  EXPECT_EQ(
      ZX_OK,
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr),
      "map");
  EXPECT_NE(ptr, 0, "map address");
  p = (volatile uint32_t *)ptr;

  // clone it and map that
  clone_vmo = ZX_HANDLE_INVALID;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone_vmo), "vm_clone");
  EXPECT_NE(ZX_HANDLE_INVALID, clone_vmo, "vm_clone_handle");
  EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, clone_vmo, 0,
                        size, &clone_ptr),
            "map");
  EXPECT_NE(clone_ptr, 0, "map address");
  cp = (volatile uint32_t *)clone_ptr;

  // write to parent and make sure clone doesn't see it
  memset((void *)p, 0x99, PAGE_SIZE);
  EXPECT_EQ(0x99999999, p[0], "wrote to original");
  EXPECT_EQ(0, cp[0], "read back from clone");

  EXPECT_OK(zx_vmo_op_range(clone_vmo, ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, NULL, 0));

  // make sure that clone still has different contents
  EXPECT_EQ(0, cp[0], "read back from clone");
  EXPECT_EQ(0x99999999, p[0], "read back from original");

  // write to clone and make sure parent doesn't see it
  cp[0] = 0x44444444;
  EXPECT_EQ(0x44444444, cp[0], "wrote to clone");
  EXPECT_EQ(0x99999999, p[0], "read back from original");

  // close the original handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");

  // close the clone handle
  EXPECT_OK(zx_handle_close(clone_vmo), "handle_close");

  // unmap
  EXPECT_OK(zx_vmar_unmap(zx_vmar_root_self(), ptr, size), "unmap");
  EXPECT_OK(zx_vmar_unmap(zx_vmar_root_self(), clone_ptr, size), "unmap");
}

// Returns zero on failure.
zx_rights_t GetHandleRights(zx_handle_t h) {
  zx_info_handle_basic_t info;
  zx_status_t s =
      zx_object_get_info(h, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (s != ZX_OK) {
    EXPECT_OK(s);  // Poison the test
    return 0;
  }
  return info.rights;
}

TEST(VmoCloneTestCase, Rights) {
  static const char kOldVmoName[] = "original";
  static const char kNewVmoName[] = "clone";

  static const zx_rights_t kOldVmoRights = ZX_RIGHT_READ | ZX_RIGHT_DUPLICATE;
  static const zx_rights_t kNewVmoRights =
      kOldVmoRights | ZX_RIGHT_WRITE | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY;

  zx_handle_t vmo;
  ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &vmo), ZX_OK);
  ASSERT_EQ(zx_object_set_property(vmo, ZX_PROP_NAME, kOldVmoName, sizeof(kOldVmoName)), ZX_OK);
  ASSERT_EQ(GetHandleRights(vmo) & kOldVmoRights, kOldVmoRights);

  zx_handle_t reduced_rights_vmo;
  ASSERT_EQ(zx_handle_duplicate(vmo, kOldVmoRights, &reduced_rights_vmo), ZX_OK);
  EXPECT_EQ(GetHandleRights(reduced_rights_vmo), kOldVmoRights);

  zx_handle_t clone;
  ASSERT_EQ(
      zx_vmo_create_child(reduced_rights_vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &clone),
      ZX_OK);

  EXPECT_OK(zx_handle_close(reduced_rights_vmo));

  ASSERT_EQ(zx_object_set_property(clone, ZX_PROP_NAME, kNewVmoName, sizeof(kNewVmoName)), ZX_OK);

  char oldname[ZX_MAX_NAME_LEN] = "bad";
  EXPECT_EQ(zx_object_get_property(vmo, ZX_PROP_NAME, oldname, sizeof(oldname)), ZX_OK);
  EXPECT_STR_EQ(oldname, kOldVmoName, "original VMO name");

  char newname[ZX_MAX_NAME_LEN] = "bad";
  EXPECT_EQ(zx_object_get_property(clone, ZX_PROP_NAME, newname, sizeof(newname)), ZX_OK);
  EXPECT_STR_EQ(newname, kNewVmoName, "clone VMO name");

  EXPECT_OK(zx_handle_close(vmo));
  EXPECT_EQ(GetHandleRights(clone), kNewVmoRights);
  EXPECT_OK(zx_handle_close(clone));
}

// Check that non-resizable VMOs cannot get resized.
TEST(VmoCloneTestCase, NoResize) {
  const size_t len = PAGE_SIZE * 4;
  zx_handle_t parent = ZX_HANDLE_INVALID;
  zx_handle_t vmo = ZX_HANDLE_INVALID;

  zx_vmo_create(len, 0, &parent);
  zx_vmo_create_child(parent, ZX_VMO_CHILD_COPY_ON_WRITE, 0, len, &vmo);

  EXPECT_NE(vmo, ZX_HANDLE_INVALID);

  zx_status_t status;
  status = zx_vmo_set_size(vmo, len + PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "vm_object_set_size");

  status = zx_vmo_set_size(vmo, len - PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "vm_object_set_size");

  size_t size;
  status = zx_vmo_get_size(vmo, &size);
  EXPECT_OK(status, "vm_object_get_size");
  EXPECT_EQ(len, size, "vm_object_get_size");

  uintptr_t ptr;
  status = zx_vmar_map(zx_vmar_root_self(),
                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_REQUIRE_NON_RESIZABLE, 0, vmo, 0,
                       len, &ptr);
  ASSERT_OK(status, "vm_map");
  ASSERT_NE(ptr, 0, "vm_map");

  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "unmap");

  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

}  // namespace
