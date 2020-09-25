// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/fzl/memory-probe.h>
#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <thread>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

#include "helpers.h"

extern "C" __WEAK zx_handle_t get_root_resource(void);

namespace {

TEST(VmoTestCase, Create) {
  zx_status_t status;
  zx_handle_t vmo[16];

  // allocate a bunch of vmos then free them
  for (size_t i = 0; i < std::size(vmo); i++) {
    status = zx_vmo_create(i * PAGE_SIZE, 0, &vmo[i]);
    EXPECT_OK(status, "vm_object_create");
  }

  for (size_t i = 0; i < std::size(vmo); i++) {
    status = zx_handle_close(vmo[i]);
    EXPECT_OK(status, "handle_close");
  }
}

TEST(VmoTestCase, ReadWriteBadLen) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object and attempt read/write from it, with bad length
  const size_t len = PAGE_SIZE * 4;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  char buf[len];
  for (int i = 1; i <= 2; i++) {
    status = zx_vmo_read(vmo, buf, 0, std::numeric_limits<size_t>::max() - (PAGE_SIZE / i));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
    status = zx_vmo_write(vmo, buf, 0, std::numeric_limits<size_t>::max() - (PAGE_SIZE / i));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }
  status = zx_vmo_read(vmo, buf, 0, len);
  EXPECT_OK(status, "vmo_read");
  status = zx_vmo_write(vmo, buf, 0, len);
  EXPECT_OK(status, "vmo_write");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, ReadWrite) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object and read/write from it
  const size_t len = PAGE_SIZE * 4;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  char buf[len];
  status = zx_vmo_read(vmo, buf, 0, sizeof(buf));
  EXPECT_OK(status, "vm_object_read");

  // make sure it's full of zeros
  size_t count = 0;
  for (auto c : buf) {
    EXPECT_EQ(c, 0, "zero test");
    if (c != 0) {
      printf("char at offset %#zx is bad\n", count);
    }
    count++;
  }

  memset(buf, 0x99, sizeof(buf));
  status = zx_vmo_write(vmo, buf, 0, sizeof(buf));
  EXPECT_OK(status, "vm_object_write");

  // map it
  uintptr_t ptr;
  status =
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, len, &ptr);
  EXPECT_OK(status, "vm_map");
  EXPECT_NE(0u, ptr, "vm_map");

  // check that it matches what we last wrote into it
  EXPECT_BYTES_EQ((uint8_t *)buf, (uint8_t *)ptr, sizeof(buf), "mapped buffer");

  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "vm_unmap");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, ReadWriteRange) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object
  const size_t len = PAGE_SIZE * 4;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // fail to read past end
  char buf[len * 2];
  status = zx_vmo_read(vmo, buf, 0, sizeof(buf));
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read past end");

  // Successfully read 0 bytes at end
  status = zx_vmo_read(vmo, buf, len, 0);
  EXPECT_OK(status, "vm_object_read zero at end");

  // Fail to read 0 bytes past end
  status = zx_vmo_read(vmo, buf, len + 1, 0);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read zero past end");

  // fail to write past end
  status = zx_vmo_write(vmo, buf, 0, sizeof(buf));
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write past end");

  // Successfully write 0 bytes at end
  status = zx_vmo_write(vmo, buf, len, 0);
  EXPECT_OK(status, "vm_object_write zero at end");

  // Fail to read 0 bytes past end
  status = zx_vmo_write(vmo, buf, len + 1, 0);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write zero past end");

  // Test for unsigned wraparound
  status = zx_vmo_read(vmo, buf, UINT64_MAX - (len / 2), len);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_read offset + len wraparound");
  status = zx_vmo_write(vmo, buf, UINT64_MAX - (len / 2), len);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "vm_object_write offset + len wraparound");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, Map) {
  zx_status_t status;
  zx_handle_t vmo;
  uintptr_t ptr[3] = {};

  // allocate a vmo
  status = zx_vmo_create(4 * PAGE_SIZE, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // do a regular map
  ptr[0] = 0;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, PAGE_SIZE, &ptr[0]);
  EXPECT_OK(status, "map");
  EXPECT_NE(0u, ptr[0], "map address");
  // printf("mapped %#" PRIxPTR "\n", ptr[0]);

  // try to map something completely out of range without any fixed mapping, should succeed
  ptr[2] = UINTPTR_MAX;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, PAGE_SIZE, &ptr[2]);
  EXPECT_OK(status, "map");
  EXPECT_NE(0u, ptr[2], "map address");

  // try to map something completely out of range fixed, should fail
  uintptr_t map_addr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_SPECIFIC, UINTPTR_MAX, vmo, 0,
                       PAGE_SIZE, &map_addr);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "map");

  // cleanup
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");

  for (auto p : ptr) {
    if (p) {
      status = zx_vmar_unmap(zx_vmar_root_self(), p, PAGE_SIZE);
      EXPECT_OK(status, "unmap");
    }
  }
}

TEST(VmoTestCase, MapRead) {
  zx::vmo vmo;

  EXPECT_OK(zx::vmo::create(PAGE_SIZE * 2, 0, &vmo));

  uintptr_t vaddr;
  // Map in the first page
  EXPECT_OK(
      zx::vmar::root_self()->map(0, vmo, 0, PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &vaddr));

  // Read from the second page of the vmo to mapping.
  // This should succeed and not deadlock in the kernel.
  EXPECT_OK(vmo.read(reinterpret_cast<void *>(vaddr), PAGE_SIZE, PAGE_SIZE));
}

TEST(VmoTestCase, ParallelRead) {
  constexpr size_t kNumPages = 1024;
  zx::vmo vmo1, vmo2;

  EXPECT_OK(zx::vmo::create(PAGE_SIZE * kNumPages, 0, &vmo1));
  EXPECT_OK(zx::vmo::create(PAGE_SIZE * kNumPages, 0, &vmo2));

  uintptr_t vaddr1, vaddr2;
  // Map the bottom half of both in.
  EXPECT_OK(zx::vmar::root_self()->map(0, vmo1, 0, PAGE_SIZE * (kNumPages / 2),
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &vaddr1));
  EXPECT_OK(zx::vmar::root_self()->map(0, vmo2, 0, PAGE_SIZE * (kNumPages / 2),
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &vaddr2));

  // Spin up a thread to read from one of the vmos, whilst we read from the other
  auto vmo_read_closure = [&vmo1, &vaddr2] {
    vmo1.read(reinterpret_cast<void *>(vaddr2), PAGE_SIZE * (kNumPages / 2),
              PAGE_SIZE * (kNumPages / 2));
  };
  std::thread thread(vmo_read_closure);
  // As these two threads read from one vmo into the mapping from another vmo if there are any
  // scenarios where the kernel would try and hold both vmo locks at the same time (without
  // attempting to resolve lock ordering) then this should trigger a deadlock to occur.
  EXPECT_OK(vmo2.read(reinterpret_cast<void *>(vaddr1), PAGE_SIZE * (kNumPages / 2),
                      PAGE_SIZE * (kNumPages / 2)));
  thread.join();
}

TEST(VmoTestCase, ReadOnlyMap) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object and read/write from it
  const size_t len = PAGE_SIZE;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // map it
  uintptr_t ptr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, len, &ptr);
  EXPECT_OK(status, "vm_map");
  EXPECT_NE(0u, ptr, "vm_map");

  EXPECT_EQ(false, probe_for_write((void *)ptr), "write");

  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "vm_unmap");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, NoPermMap) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object and read/write from it
  const size_t len = PAGE_SIZE;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // map it with read permissions
  uintptr_t ptr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, len, &ptr);
  EXPECT_OK(status, "vm_map");
  EXPECT_NE(0u, ptr, "vm_map");

  // protect it to no permissions
  status = zx_vmar_protect(zx_vmar_root_self(), 0, ptr, len);
  EXPECT_OK(status, "vm_protect");

  // test reading writing to the mapping
  EXPECT_EQ(false, probe_for_read(reinterpret_cast<void *>(ptr)), "read");
  EXPECT_EQ(false, probe_for_write(reinterpret_cast<void *>(ptr)), "write");

  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "vm_unmap");

  // close the handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

TEST(VmoTestCase, NoPermProtect) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object and read/write from it
  const size_t len = PAGE_SIZE;
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // map it with no permissions
  uintptr_t ptr;
  status = zx_vmar_map(zx_vmar_root_self(), 0, 0, vmo, 0, len, &ptr);
  EXPECT_OK(status, "vm_map");
  EXPECT_NE(0u, ptr, "vm_map");

  // test writing to the mapping
  EXPECT_EQ(false, probe_for_write(reinterpret_cast<void *>(ptr)), "write");
  // test reading to the mapping
  EXPECT_EQ(false, probe_for_read(reinterpret_cast<void *>(ptr)), "read");

  // protect it to read permissions and make sure it works as expected
  status = zx_vmar_protect(zx_vmar_root_self(), ZX_VM_PERM_READ, ptr, len);
  EXPECT_OK(status, "vm_protect");

  // test writing to the mapping
  EXPECT_EQ(false, probe_for_write(reinterpret_cast<void *>(ptr)), "write");

  // test reading from the mapping
  EXPECT_EQ(true, probe_for_read(reinterpret_cast<void *>(ptr)), "read");

  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "vm_unmap");

  // close the handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

TEST(VmoTestCase, Resize) {
  zx_status_t status;
  zx_handle_t vmo;

  // allocate an object
  size_t len = PAGE_SIZE * 4;
  status = zx_vmo_create(len, ZX_VMO_RESIZABLE, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // get the size that we set it to
  uint64_t size = 0x99999999;
  status = zx_vmo_get_size(vmo, &size);
  EXPECT_OK(status, "vm_object_get_size");
  EXPECT_EQ(len, size, "vm_object_get_size");

  // try to resize it
  len += PAGE_SIZE;
  status = zx_vmo_set_size(vmo, len);
  EXPECT_OK(status, "vm_object_set_size");

  // get the size again
  size = 0x99999999;
  status = zx_vmo_get_size(vmo, &size);
  EXPECT_OK(status, "vm_object_get_size");
  EXPECT_EQ(len, size, "vm_object_get_size");

  // try to resize it to a ludicrous size
  status = zx_vmo_set_size(vmo, UINT64_MAX);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "vm_object_set_size too big");

  // resize it to a non aligned size
  status = zx_vmo_set_size(vmo, len + 1);
  EXPECT_OK(status, "vm_object_set_size");

  // size should be rounded up to the next page boundary
  size = 0x99999999;
  status = zx_vmo_get_size(vmo, &size);
  EXPECT_OK(status, "vm_object_get_size");
  EXPECT_EQ(fbl::round_up(len + 1u, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");
  len = fbl::round_up(len + 1u, static_cast<size_t>(PAGE_SIZE));

  // map it
  uintptr_t ptr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, len, &ptr);
  EXPECT_OK(status, "vm_map");
  EXPECT_NE(ptr, 0, "vm_map");

  // attempt to map expecting an non resizable vmo.
  uintptr_t ptr2;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_REQUIRE_NON_RESIZABLE, 0, vmo,
                       0, len, &ptr2);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status, "vm_map");

  // resize it with it mapped
  status = zx_vmo_set_size(vmo, size);
  EXPECT_OK(status, "vm_object_set_size");

  // unmap it
  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
  EXPECT_OK(status, "unmap");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

// Check that non-resizable VMOs cannot get resized.
TEST(VmoTestCase, NoResize) {
  const size_t len = PAGE_SIZE * 4;
  zx_handle_t vmo = ZX_HANDLE_INVALID;

  zx_vmo_create(len, 0, &vmo);

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

TEST(VmoTestCase, Info) {
  size_t len = PAGE_SIZE * 4;
  zx::vmo vmo;
  zx_info_vmo_t info;
  zx_status_t status;

  // Create a non-resizeable VMO, query the INFO on it
  // and dump it.
  status = zx::vmo::create(len, 0, &vmo);
  EXPECT_OK(status, "vm_info_test: vmo_create");

  status = vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
  EXPECT_OK(status, "vm_info_test: info_vmo");

  vmo.reset();

  EXPECT_EQ(info.size_bytes, len, "vm_info_test: info_vmo.size_bytes");
  EXPECT_EQ(info.flags, ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_VIA_HANDLE,
            "vm_info_test: info_vmo.flags");
  EXPECT_EQ(info.cache_policy, ZX_CACHE_POLICY_CACHED, "vm_info_test: info_vmo.cache_policy");

  // Create a resizeable uncached VMO, query the INFO on it and dump it.
  len = PAGE_SIZE * 8;
  zx::vmo::create(len, ZX_VMO_RESIZABLE, &vmo);
  EXPECT_OK(status, "vm_info_test: vmo_create");
  vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);

  size_t actual, avail;
  status = vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), &actual, &avail);
  EXPECT_OK(status, "vm_info_test: info_vmo");
  EXPECT_EQ(actual, 1);
  EXPECT_EQ(avail, 1);

  vmo.reset();

  EXPECT_EQ(info.size_bytes, len, "vm_info_test: info_vmo.size_bytes");
  EXPECT_EQ(info.flags, ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_VIA_HANDLE | ZX_INFO_VMO_RESIZABLE,
            "vm_info_test: info_vmo.flags");
  EXPECT_EQ(info.cache_policy, ZX_CACHE_POLICY_UNCACHED, "vm_info_test: info_vmo.cache_policy");

  if (get_root_resource) {
    zx::iommu iommu;
    zx::bti bti;
    auto final_bti_check = vmo_test::CreateDeferredBtiCheck(bti);

    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    zx::unowned_resource root_res(get_root_resource());
    zx_iommu_desc_dummy_t desc;
    EXPECT_EQ(zx_iommu_create((*root_res).get(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                              iommu.reset_and_get_address()),
              ZX_OK);
    bti = vmo_test::CreateNamedBti(iommu, 0, 0xdeadbeef, "VmoTestCase::Info");

    len = PAGE_SIZE * 12;
    EXPECT_OK(zx::vmo::create_contiguous(bti, len, 0, &vmo));

    status = vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr);
    EXPECT_OK(status, "vm_info_test: info_vmo");

    EXPECT_EQ(info.size_bytes, len, "vm_info_test: info_vmo.size_bytes");
    EXPECT_EQ(info.flags, ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_VIA_HANDLE | ZX_INFO_VMO_CONTIGUOUS,
              "vm_info_test: info_vmo.flags");
    EXPECT_EQ(info.cache_policy, ZX_CACHE_POLICY_CACHED, "vm_info_test: info_vmo.cache_policy");
  }
}

TEST(VmoTestCase, SizeAlign) {
  for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
    zx_handle_t vmo;

    // create a new object with nonstandard size
    zx_status_t status = zx_vmo_create(s, 0, &vmo);
    EXPECT_OK(status, "vm_object_create");

    // should be the size rounded up to the nearest page boundary
    uint64_t size = 0x99999999;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_OK(status, "vm_object_get_size");
    EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");

    // close the handle
    EXPECT_OK(zx_handle_close(vmo), "handle_close");
  }
}

TEST(VmoTestCase, ResizeAlign) {
  // resize a vmo with a particular size and test that the resulting size is aligned on a page
  // boundary.
  zx_handle_t vmo;
  zx_status_t status = zx_vmo_create(0, ZX_VMO_RESIZABLE, &vmo);
  EXPECT_OK(status, "vm_object_create");

  for (uint64_t s = 0; s < PAGE_SIZE * 4; s++) {
    // set the size of the object
    zx_status_t status = zx_vmo_set_size(vmo, s);
    EXPECT_OK(status, "vm_object_create");

    // should be the size rounded up to the nearest page boundary
    uint64_t size = 0x99999999;
    status = zx_vmo_get_size(vmo, &size);
    EXPECT_OK(status, "vm_object_get_size");
    EXPECT_EQ(fbl::round_up(s, static_cast<size_t>(PAGE_SIZE)), size, "vm_object_get_size");
  }

  // close the handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

TEST(VmoTestCase, ContentSize) {
  zx_status_t status;
  zx::vmo vmo;

  size_t len = PAGE_SIZE * 4;
  status = zx::vmo::create(len, 0, &vmo);
  EXPECT_OK(status, "zx::vmo::create");

  uint64_t content_size = 42;
  status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  EXPECT_OK(status, "get_property");
  EXPECT_EQ(0u, content_size);

  uint64_t target_size = len / 3;
  status = vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &target_size, sizeof(target_size));
  EXPECT_OK(status, "set_property");

  content_size = 42;
  status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  EXPECT_OK(status, "get_property");
  EXPECT_EQ(target_size, content_size);

  target_size = len + 15643;
  status = vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &target_size, sizeof(target_size));
  EXPECT_OK(status, "set_property");

  content_size = 42;
  status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  EXPECT_OK(status, "get_property");
  EXPECT_EQ(target_size, content_size);
}

void RightsTestMapHelper(zx_handle_t vmo, size_t len, uint32_t flags, bool expect_success,
                         zx_status_t fail_err_code) {
  uintptr_t ptr;

  zx_status_t r = zx_vmar_map(zx_vmar_root_self(), flags, 0, vmo, 0, len, &ptr);
  if (expect_success) {
    EXPECT_OK(r);

    r = zx_vmar_unmap(zx_vmar_root_self(), ptr, len);
    EXPECT_OK(r, "unmap");
  } else {
    EXPECT_EQ(fail_err_code, r);
  }
}

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

void ChildPermsTestHelper(const zx_handle_t vmo) {
  // Read out the current rights;
  const zx_rights_t parent_rights = GetHandleRights(vmo);

  // Make different kinds of children and ensure we get the correct rights.
  zx_handle_t child;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, PAGE_SIZE, &child));
  EXPECT_EQ(GetHandleRights(child),
            (parent_rights | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY | ZX_RIGHT_WRITE) &
                ~ZX_RIGHT_EXECUTE);
  EXPECT_OK(zx_handle_close(child));

  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE | ZX_VMO_CHILD_NO_WRITE, 0,
                                PAGE_SIZE, &child));
  EXPECT_EQ(GetHandleRights(child),
            (parent_rights | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY) & ~ZX_RIGHT_WRITE);
  EXPECT_OK(zx_handle_close(child));

  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_SLICE, 0, PAGE_SIZE, &child));
  EXPECT_EQ(GetHandleRights(child), parent_rights | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY);
  EXPECT_OK(zx_handle_close(child));

  EXPECT_OK(
      zx_vmo_create_child(vmo, ZX_VMO_CHILD_SLICE | ZX_VMO_CHILD_NO_WRITE, 0, PAGE_SIZE, &child));
  EXPECT_EQ(GetHandleRights(child),
            (parent_rights | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY) & ~ZX_RIGHT_WRITE);
  EXPECT_OK(zx_handle_close(child));
}

TEST(VmoTestCase, Rights) {
  char buf[4096];
  size_t len = PAGE_SIZE * 4;
  zx_status_t status;
  zx_handle_t vmo, vmo2;

  // allocate an object
  status = zx_vmo_create(len, 0, &vmo);
  EXPECT_OK(status, "vm_object_create");

  // Check that the handle has at least the expected rights.
  // This list should match the list in docs/syscalls/vmo_create.md.
  static const zx_rights_t kExpectedRights =
      ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_WAIT | ZX_RIGHT_READ | ZX_RIGHT_WRITE |
      ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY;
  EXPECT_EQ(kExpectedRights, kExpectedRights & GetHandleRights(vmo));

  // test that we can read/write it
  status = zx_vmo_read(vmo, buf, 0, 0);
  EXPECT_EQ(0, status, "vmo_read");
  status = zx_vmo_write(vmo, buf, 0, 0);
  EXPECT_EQ(0, status, "vmo_write");

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_READ, &vmo2);
  status = zx_vmo_read(vmo2, buf, 0, 0);
  EXPECT_EQ(0, status, "vmo_read");
  status = zx_vmo_write(vmo2, buf, 0, 0);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_write");
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_WRITE, &vmo2);
  status = zx_vmo_read(vmo2, buf, 0, 0);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_read");
  status = zx_vmo_write(vmo2, buf, 0, 0);
  EXPECT_EQ(0, status, "vmo_write");
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, 0, &vmo2);
  status = zx_vmo_read(vmo2, buf, 0, 0);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_read");
  status = zx_vmo_write(vmo2, buf, 0, 0);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, status, "vmo_write");
  zx_handle_close(vmo2);

  status = zx_vmo_replace_as_executable(vmo, ZX_HANDLE_INVALID, &vmo);
  EXPECT_OK(status, "vmo_replace_as_executable");
  EXPECT_EQ(kExpectedRights | ZX_RIGHT_EXECUTE,
            (kExpectedRights | ZX_RIGHT_EXECUTE) & GetHandleRights(vmo));

  // full perm test
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo, len, ZX_VM_PERM_READ, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(
      vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0));

  // try most of the permutations of mapping and clone a vmo with various rights dropped
  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE | ZX_RIGHT_DUPLICATE,
                      &vmo2);
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo2));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, false, ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, false, ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false,
                                               ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false,
                          ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,
                                               false, ZX_ERR_ACCESS_DENIED));
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE, &vmo2);
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo2));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false,
                                               ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false,
                          ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,
                                               false, ZX_ERR_ACCESS_DENIED));
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE, &vmo2);
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, false, ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false,
                                               ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false,
                          ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,
                                               false, ZX_ERR_ACCESS_DENIED));
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE,
                      &vmo2);
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo2));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false,
                          ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,
                                               false, ZX_ERR_ACCESS_DENIED));
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(vmo, ZX_RIGHT_READ | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE,
                      &vmo2);
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo2));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, false,
                                               ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, false,
                          ZX_ERR_ACCESS_DENIED));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0));
  zx_handle_close(vmo2);

  vmo2 = ZX_HANDLE_INVALID;
  zx_handle_duplicate(
      vmo, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_EXECUTE | ZX_RIGHT_MAP | ZX_RIGHT_DUPLICATE,
      &vmo2);
  ASSERT_NO_FATAL_FAILURES(ChildPermsTestHelper(vmo2));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, 0, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_WRITE, false, ZX_ERR_INVALID_ARGS));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, true, 0));
  ASSERT_NO_FATAL_FAILURES(RightsTestMapHelper(
      vmo2, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE, true, 0));
  ASSERT_NO_FATAL_FAILURES(
      RightsTestMapHelper(vmo, len, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, true, 0));
  zx_handle_close(vmo2);

  // test that we can get/set a property on it
  const char *set_name = "test vmo";
  status = zx_object_set_property(vmo, ZX_PROP_NAME, set_name, sizeof(set_name));
  EXPECT_OK(status, "set_property");
  char get_name[ZX_MAX_NAME_LEN];
  status = zx_object_get_property(vmo, ZX_PROP_NAME, get_name, sizeof(get_name));
  EXPECT_OK(status, "get_property");
  EXPECT_STR_EQ(set_name, get_name, "vmo name");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");

  // Use wrong handle with wrong permission, and expect wrong type not
  // ZX_ERR_ACCESS_DENIED
  vmo = ZX_HANDLE_INVALID;
  vmo2 = ZX_HANDLE_INVALID;
  status = zx_port_create(0, &vmo);
  EXPECT_OK(status, "zx_port_create");
  status = zx_handle_duplicate(vmo, 0, &vmo2);
  EXPECT_OK(status, "zx_handle_duplicate");
  status = zx_vmo_read(vmo2, buf, 0, 0);
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, status, "vmo_read wrong type");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
  status = zx_handle_close(vmo2);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, Commit) {
  zx_handle_t vmo;
  zx_status_t status;
  uintptr_t ptr, ptr2, ptr3;

  // create a vmo
  const size_t size = 16384;

  status = zx_vmo_create(size, 0, &vmo);
  EXPECT_EQ(0, status, "vm_object_create");

  // commit a range of it
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
  EXPECT_EQ(0, status, "vm commit");

  // decommit that range
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, size, nullptr, 0);
  EXPECT_EQ(0, status, "vm decommit");

  // commit a range of it
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, nullptr, 0);
  EXPECT_EQ(0, status, "vm commit");

  // map it
  ptr = 0;
  status =
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &ptr);
  EXPECT_OK(status, "map");
  EXPECT_NE(ptr, 0, "map address");

  // second mapping with an offset
  ptr2 = 0;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, PAGE_SIZE,
                       size, &ptr2);
  EXPECT_OK(status, "map2");
  EXPECT_NE(ptr2, 0, "map address2");

  // third mapping with a totally non-overlapping offset
  ptr3 = 0;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, size * 2,
                       size, &ptr3);
  EXPECT_OK(status, "map3");
  EXPECT_NE(ptr3, 0, "map address3");

  // write into it at offset PAGE_SIZE, read it back
  volatile uint32_t *u32 = (volatile uint32_t *)(ptr + PAGE_SIZE);
  *u32 = 99;
  EXPECT_EQ(99u, (*u32), "written memory");

  // check the alias
  volatile uint32_t *u32a = (volatile uint32_t *)(ptr2);
  EXPECT_EQ(99u, (*u32a), "written memory");

  // decommit page 0
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0, PAGE_SIZE, nullptr, 0);
  EXPECT_EQ(0, status, "vm decommit");

  // verify that it didn't get unmapped
  EXPECT_EQ(99u, (*u32), "written memory");
  // verify that it didn't get unmapped
  EXPECT_EQ(99u, (*u32a), "written memory2");

  // decommit page 1
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, PAGE_SIZE, PAGE_SIZE, nullptr, 0);
  EXPECT_EQ(0, status, "vm decommit");

  // verify that it did get unmapped
  EXPECT_EQ(0u, (*u32), "written memory");
  // verify that it did get unmapped
  EXPECT_EQ(0u, (*u32a), "written memory2");

  // unmap our vmos
  status = zx_vmar_unmap(zx_vmar_root_self(), ptr, size);
  EXPECT_OK(status, "vm_unmap");
  status = zx_vmar_unmap(zx_vmar_root_self(), ptr2, size);
  EXPECT_OK(status, "vm_unmap");
  status = zx_vmar_unmap(zx_vmar_root_self(), ptr3, size);
  EXPECT_OK(status, "vm_unmap");

  // close the handle
  status = zx_handle_close(vmo);
  EXPECT_OK(status, "handle_close");
}

TEST(VmoTestCase, ZeroPage) {
  zx_handle_t vmo;
  zx_status_t status;
  uintptr_t ptr[3];

  // create a vmo
  const size_t size = PAGE_SIZE * 4;

  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "vm_object_create");

  // make a few mappings of the vmo
  for (auto &p : ptr) {
    EXPECT_EQ(
        ZX_OK,
        zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &p),
        "map");
    EXPECT_NE(0, p, "map address");
  }

  volatile uint32_t *val = (volatile uint32_t *)ptr[0];
  volatile uint32_t *val2 = (volatile uint32_t *)ptr[1];
  volatile uint32_t *val3 = (volatile uint32_t *)ptr[2];

  // read fault in the first mapping
  EXPECT_EQ(0, *val, "read zero");

  // write fault the second mapping
  *val2 = 99;
  EXPECT_EQ(99, *val2, "read back 99");

  // expect the third mapping to read fault in the new page
  EXPECT_EQ(99, *val3, "read 99");

  // expect the first mapping to have gotten updated with the new mapping
  // and no longer be mapping the zero page
  EXPECT_EQ(99, *val, "read 99 from former zero page");

  // read fault in zeros on the second page
  val = (volatile uint32_t *)(ptr[0] + PAGE_SIZE);
  EXPECT_EQ(0, *val, "read zero");

  // write to the page via a vmo_write call
  uint32_t v = 100;
  status = zx_vmo_write(vmo, &v, PAGE_SIZE, sizeof(v));
  EXPECT_OK(status, "writing to vmo");

  // expect it to read back the new value
  EXPECT_EQ(100, *val, "read 100 from former zero page");

  // read fault in zeros on the third page
  val = (volatile uint32_t *)(ptr[0] + PAGE_SIZE * 2);
  EXPECT_EQ(0, *val, "read zero");

  // commit this range of the vmo via a commit call
  status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, PAGE_SIZE * 2, PAGE_SIZE, nullptr, 0);
  EXPECT_OK(status, "committing memory");

  // write to the third page
  status = zx_vmo_write(vmo, &v, PAGE_SIZE * 2, sizeof(v));
  EXPECT_OK(status, "writing to vmo");

  // expect it to read back the new value
  EXPECT_EQ(100, *val, "read 100 from former zero page");

  // unmap
  for (auto p : ptr)
    EXPECT_OK(zx_vmar_unmap(zx_vmar_root_self(), p, size), "unmap");

  // close the handle
  EXPECT_OK(zx_handle_close(vmo), "handle_close");
}

TEST(VmoTestCase, Cache) {
  zx_handle_t vmo;
  const size_t size = PAGE_SIZE;

  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "creation for cache_policy");

  // clean vmo can have all valid cache policies set
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED_DEVICE));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_WRITE_COMBINING));

  // bad cache policy
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_MASK + 1));

  // map the vmo, make sure policy doesn't set
  uintptr_t ptr;
  EXPECT_OK(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &ptr));
  EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_vmar_unmap(zx_vmar_root_self(), ptr, size));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

  // clone the vmo, make sure policy doesn't set
  zx_handle_t clone;
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
  EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_handle_close(clone));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));

  // clone the vmo, try to set policy on the clone
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
  EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_set_cache_policy(clone, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_handle_close(clone));

  // set the policy, make sure future clones do not go through
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
  EXPECT_EQ(ZX_ERR_BAD_STATE,
            zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_vmo_create_child(vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, size, &clone));
  EXPECT_OK(zx_handle_close(clone));

  // set the policy, make sure vmo read/write do not work
  char c;
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_UNCACHED));
  EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_read(vmo, &c, 0, sizeof(c)));
  EXPECT_EQ(ZX_ERR_BAD_STATE, zx_vmo_write(vmo, &c, 0, sizeof(c)));
  EXPECT_OK(zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_CACHED));
  EXPECT_OK(zx_vmo_read(vmo, &c, 0, sizeof(c)));
  EXPECT_OK(zx_vmo_write(vmo, &c, 0, sizeof(c)));

  EXPECT_OK(zx_handle_close(vmo), "close handle");
}

TEST(VmoTestCase, PhysicalSlice) {
  vmo_test::PhysVmo phys;
  if (auto res = vmo_test::GetTestPhysVmo(); !res.is_ok()) {
    if (res.error_value() == ZX_ERR_NOT_SUPPORTED) {
      printf("Root resource not available, skipping\n");
    }
    return;
  } else {
    phys = std::move(res.value());
  }

  const size_t size = PAGE_SIZE * 2;
  ASSERT_GE(phys.size, size);

  // Switch to a cached policy as we are operating on real memory and do not need to be uncached.
  EXPECT_OK(phys.vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED));

  zx_info_vmo_t phys_vmo_info;
  ASSERT_OK(
      phys.vmo.get_info(ZX_INFO_VMO, &phys_vmo_info, sizeof(phys_vmo_info), nullptr, nullptr));
  ASSERT_NE(phys_vmo_info.koid, 0ul);
  ASSERT_EQ(phys_vmo_info.parent_koid, 0ul);

  // Create a slice of the second page.
  zx::vmo slice_vmo;
  EXPECT_OK(phys.vmo.create_child(ZX_VMO_CHILD_SLICE, size / 2, size / 2, &slice_vmo));

  // Sliced VMO should have correct parent_koid in its VMO info struct.
  zx_info_vmo_t slice_vmo_info;
  ASSERT_OK(
      slice_vmo.get_info(ZX_INFO_VMO, &slice_vmo_info, sizeof(slice_vmo_info), nullptr, nullptr));
  ASSERT_EQ(slice_vmo_info.parent_koid, phys_vmo_info.koid);

  // Map both VMOs in so we can access them.
  uintptr_t parent_vaddr;
  EXPECT_OK(zx::vmar::root_self()->map(0, phys.vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                       &parent_vaddr));
  uintptr_t slice_vaddr;
  EXPECT_OK(zx::vmar::root_self()->map(0, slice_vmo, 0, size / 2,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &slice_vaddr));

  // Just do some tests using the first byte of each page
  char *parent_private_test = (char *)parent_vaddr;
  char *parent_shared_test = (char *)(parent_vaddr + size / 2);
  char *slice_test = (char *)slice_vaddr;

  // We expect parent_shared_test and slice_test to be accessing the same physical pages, but we
  // should have gotten different mappings for them.
  EXPECT_NE(parent_shared_test, slice_test);

  *parent_private_test = 0;
  *parent_shared_test = 1;

  // This should have set the child.
  EXPECT_EQ(*slice_test, 1);

  // Write to the child now and validate parent changed correctly.
  *slice_test = 42;
  EXPECT_EQ(*parent_shared_test, 42);
  EXPECT_EQ(*parent_private_test, 0);
}

TEST(VmoTestCase, CacheOp) {
  constexpr size_t normal_size = 0x8000;
  zx_handle_t normal_vmo = ZX_HANDLE_INVALID;

  EXPECT_OK(zx_vmo_create(normal_size, 0, &normal_vmo), "creation for cache op (normal vmo)");

  vmo_test::PhysVmo phys;
  if (auto res = vmo_test::GetTestPhysVmo(); res.is_ok()) {
    phys = std::move(res.value());
    // Go ahead and set the cache policy; we don't want the op_range calls
    // below to potentially skip running any code.
    EXPECT_OK(phys.vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED), "zx_vmo_set_cache_policy");
    ASSERT_GE(phys.size, normal_size);
  }

  auto test_vmo = [](zx_handle_t vmo, size_t size) {
    if (vmo == ZX_HANDLE_INVALID) {
      return;
    }

    auto test_op = [vmo, size](uint32_t op) {
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0, 1, nullptr, 0), "0 1");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0, 1, nullptr, 0), "0 1");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 1, 1, nullptr, 0), "1 1");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0, size, nullptr, 0), "0 size");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 1, size - 1, nullptr, 0), "1 size-1");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0x5200, 1, nullptr, 0), "0x5200 1");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0x5200, 0x800, nullptr, 0), "0x5200 0x800");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0x5200, 0x1000, nullptr, 0), "0x5200 0x1000");
      EXPECT_OK(zx_vmo_op_range(vmo, op, 0x5200, 0x1200, nullptr, 0), "0x5200 0x1200");

      EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_vmo_op_range(vmo, op, 0, 0, nullptr, 0), "0 0");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, 1, size, nullptr, 0), "0 size");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, size, 1, nullptr, 0), "size 1");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, size + 1, 1, nullptr, 0), "size+1 1");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX - 1, 1, nullptr, 0),
                "UINT64_MAX-1 1");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX, 1, nullptr, 0),
                "UINT64_MAX 1");
      EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, zx_vmo_op_range(vmo, op, UINT64_MAX, UINT64_MAX, nullptr, 0),
                "UINT64_MAX UINT64_MAX");
    };

    test_op(ZX_VMO_OP_CACHE_SYNC);
    test_op(ZX_VMO_OP_CACHE_CLEAN);
    test_op(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE);
    test_op(ZX_VMO_OP_CACHE_INVALIDATE);
  };

  ASSERT_NE(ZX_HANDLE_INVALID, normal_vmo);
  test_vmo(normal_vmo, normal_size);

  // No need for a test ASSERT/EXPECT here.  If we have access to the root
  // resource, but could not create a physical VMO to test with, then
  // GetTestPhysVmo has already signaled a test failure for us.
  if (phys.vmo.is_valid()) {
    test_vmo(phys.vmo.get(), phys.size);
  }

  EXPECT_OK(zx_handle_close(normal_vmo), "close handle (normal vmo)");
  // Closing ZX_HANDLE_INVALID is not an error.
  EXPECT_OK(zx_handle_close(phys.vmo.release()), "close handle (physical vmo)");
}

TEST(VmoTestCase, CacheFlush) {
  zx_handle_t vmo;
  const size_t size = 0x8000;

  EXPECT_OK(zx_vmo_create(size, 0, &vmo), "creation for cache op");

  uintptr_t ptr_ro;
  EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &ptr_ro),
            "map");
  EXPECT_NE(ptr_ro, 0, "map address");
  void *pro = (void *)ptr_ro;

  uintptr_t ptr_rw;
  EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &ptr_rw),
            "map");
  EXPECT_NE(ptr_rw, 0, "map address");
  void *prw = (void *)ptr_rw;

  zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0, size, NULL, 0);

  EXPECT_OK(zx_cache_flush(prw, size, ZX_CACHE_FLUSH_INSN), "rw flush insn");
  EXPECT_OK(zx_cache_flush(prw, size, ZX_CACHE_FLUSH_DATA), "rw clean");
  EXPECT_OK(zx_cache_flush(prw, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INSN),
            "rw clean w/ insn");
  EXPECT_OK(zx_cache_flush(prw, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE),
            "rw clean/invalidate");
  EXPECT_EQ(ZX_OK,
            zx_cache_flush(prw, size,
                           ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE | ZX_CACHE_FLUSH_INSN),
            "rw all");

  EXPECT_OK(zx_cache_flush(pro, size, ZX_CACHE_FLUSH_INSN), "ro flush insn");
  EXPECT_OK(zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA), "ro clean");
  EXPECT_OK(zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INSN),
            "ro clean w/ insn");
  EXPECT_OK(zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE),
            "ro clean/invalidate");
  EXPECT_OK(zx_cache_flush(pro, size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE),
            "ro clean/invalidate");
  EXPECT_EQ(ZX_OK,
            zx_cache_flush(pro, size,
                           ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE | ZX_CACHE_FLUSH_INSN),
            "ro all");

  // Above checks all valid options combinations; check that invalid
  // combinations are handled correctly here.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_cache_flush(pro, size, 0), "no args");
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_cache_flush(pro, size, ZX_CACHE_FLUSH_INVALIDATE),
            "invalidate requires data");
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            zx_cache_flush(pro, size, ZX_CACHE_FLUSH_INSN | ZX_CACHE_FLUSH_INVALIDATE),
            "invalidate requires data");
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_cache_flush(pro, size, 1u << 3), "out of range a");
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, zx_cache_flush(pro, size, ~0u), "out of range b");

  zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size);
  zx_vmar_unmap(zx_vmar_root_self(), ptr_ro, size);
  EXPECT_OK(zx_handle_close(vmo), "close handle");
}

TEST(VmoTestCase, DecommitMisaligned) {
  zx_handle_t vmo;
  EXPECT_OK(zx_vmo_create(PAGE_SIZE * 2, 0, &vmo), "creation for decommit test");

  // Forbid unaligned decommit, even if there's nothing committed.
  zx_status_t status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0x10, 0x100, NULL, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "decommitting uncommitted memory");

  status = zx_vmo_op_range(vmo, ZX_VMO_OP_COMMIT, 0x10, 0x100, NULL, 0);
  EXPECT_OK(status, "committing memory");

  status = zx_vmo_op_range(vmo, ZX_VMO_OP_DECOMMIT, 0x10, 0x100, NULL, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "decommitting memory");

  EXPECT_OK(zx_handle_close(vmo), "close handle");
}

// Resizing a regular mapped VMO causes a fault.
TEST(VmoTestCase, ResizeHazard) {
  const size_t size = PAGE_SIZE * 2;
  zx_handle_t vmo;
  ASSERT_OK(zx_vmo_create(size, ZX_VMO_RESIZABLE, &vmo));

  uintptr_t ptr_rw;
  EXPECT_OK(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &ptr_rw),
            "map");

  auto int_arr = reinterpret_cast<int *>(ptr_rw);
  EXPECT_EQ(int_arr[1], 0);

  EXPECT_OK(zx_vmo_set_size(vmo, 0u));

  EXPECT_EQ(false, probe_for_read(&int_arr[1]), "read probe");
  EXPECT_EQ(false, probe_for_write(&int_arr[1]), "write probe");

  EXPECT_OK(zx_handle_close(vmo));
  EXPECT_OK(zx_vmar_unmap(zx_vmar_root_self(), ptr_rw, size), "unmap");
}

TEST(VmoTestCase, CompressedContiguous) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  zx::unowned_resource root_res(get_root_resource());

  zx::iommu iommu;
  zx::bti bti;
  zx_iommu_desc_dummy_t desc;
  auto final_bti_check = vmo_test::CreateDeferredBtiCheck(bti);

  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));
  bti = vmo_test::CreateNamedBti(iommu, 0, 0xdeadbeef, "VmoTestCase::CompressedContiguous");

  zx_info_bti_t bti_info;
  EXPECT_OK(bti.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr));

  constexpr uint32_t kMaxAddrs = 2;
  // If the minimum contiguity is too high this won't be an effective test, but
  // the code should still work.
  uint64_t size = std::min(128ul * 1024 * 1024, bti_info.minimum_contiguity * kMaxAddrs);

  zx::vmo contig_vmo;
  EXPECT_OK(zx::vmo::create_contiguous(bti, size, 0, &contig_vmo));

  zx_paddr_t paddrs[kMaxAddrs];

  uint64_t num_addrs =
      fbl::round_up(size, bti_info.minimum_contiguity) / bti_info.minimum_contiguity;

  zx::pmt pmt;
  EXPECT_OK(
      bti.pin(ZX_BTI_COMPRESS | ZX_BTI_PERM_READ, contig_vmo, 0, size, paddrs, num_addrs, &pmt));
  pmt.unpin();

  if (num_addrs > 1) {
    zx::pmt pmt2;
    EXPECT_EQ(ZX_ERR_INVALID_ARGS,
              bti.pin(ZX_BTI_COMPRESS | ZX_BTI_PERM_READ, contig_vmo, 0, size, paddrs, 1, &pmt2));
  }
}

TEST(VmoTestCase, UncachedContiguous) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  zx::unowned_resource root_res(get_root_resource());

  zx::iommu iommu;
  zx::bti bti;
  zx_iommu_desc_dummy_t desc;
  auto final_bti_check = vmo_test::CreateDeferredBtiCheck(bti);

  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));
  bti = vmo_test::CreateNamedBti(iommu, 0, 0xdeadbeef, "VmoTestCase::UncachedContiguous");

  constexpr uint64_t kSize = PAGE_SIZE * 4;

  zx::vmo contig_vmo;
  EXPECT_OK(zx::vmo::create_contiguous(bti, kSize, 0, &contig_vmo));

  // Attempt to make the vmo uncached.
  EXPECT_OK(contig_vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED));

  // Validate that it really is uncached by making sure operations that should fail, do.
  uint64_t data = 42;
  EXPECT_EQ(contig_vmo.write(&data, 0, sizeof(data)), ZX_ERR_BAD_STATE);

  // Pin part of the vmo and validate we cannot change the cache policy whilst pinned.
  zx_paddr_t paddr;

  zx::pmt pmt;
  EXPECT_OK(bti.pin(ZX_BTI_COMPRESS | ZX_BTI_PERM_READ, contig_vmo, 0, PAGE_SIZE, &paddr, 1, &pmt));

  EXPECT_EQ(contig_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED), ZX_ERR_BAD_STATE);

  // Unpin and then validate that we cannot move committed pages from uncached->cached
  pmt.unpin();
  EXPECT_EQ(contig_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED), ZX_ERR_BAD_STATE);
}

// Test various pinning operations.  In particular, we would like to test
//
// ++ Pinning of normal VMOs, contiguous VMOs, and RAM backed physical VMOs.
// ++ Pinning of child-slices of VMOs.
// ++ Attempting to overpin regions section of VMOs, particularly overpin
//    operations which do not fit in a target child-slice, but _would_ fit within
//    the main parent VMO.  See bug 53547 for details.
TEST(VmoTestCase, PinTests) {
  if (!get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  constexpr size_t kTestPages = 6;

  zx::unowned_resource root_res(get_root_resource());

  zx::iommu iommu;
  zx::bti bti;
  zx_iommu_desc_dummy_t desc;
  auto final_bti_check = vmo_test::CreateDeferredBtiCheck(bti);

  EXPECT_OK(zx::iommu::create(*root_res, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu));
  bti = vmo_test::CreateNamedBti(iommu, 0, 0xdeadbeef, "VmoTestCase::PinTests");

  enum class VmoFlavor { Normal, Contig, Physical };
  constexpr std::array FLAVORS = {VmoFlavor::Normal, VmoFlavor::Contig, VmoFlavor::Physical};

  for (auto flavor : FLAVORS) {
    struct Level {
      Level(size_t offset_pages, size_t size_pages)
          : offset(offset_pages * PAGE_SIZE), size(size_pages * PAGE_SIZE) {}

      zx::vmo vmo;
      const size_t offset;
      const size_t size;
      size_t size_past_end = 0;
    };

    std::array levels = {
        Level{0, kTestPages},
        Level{1, kTestPages - 2},
        Level{1, kTestPages - 4},
    };

    // Create the root level of the child-slice hierarchy based on the flavor we
    // are currently testing.
    switch (flavor) {
      case VmoFlavor::Normal:
        ASSERT_OK(zx::vmo::create(levels[0].size, 0, &levels[0].vmo));
        break;

      case VmoFlavor::Contig:
        ASSERT_OK(zx::vmo::create_contiguous(bti, levels[0].size, 0, &levels[0].vmo));
        break;

      case VmoFlavor::Physical:
        if (auto res = vmo_test::GetTestPhysVmo(levels[0].size); !res.is_ok()) {
          ASSERT_OK(res.error_value());
        } else {
          levels[0].vmo = std::move(res->vmo);
          ASSERT_EQ(levels[0].size, res->size);
        }
        break;
    }

    // Now that we have our root, create each of our child slice levels.  Each
    // level will be offset by level[i].offset bytes into level[i - 1].vmo.
    for (size_t i = 1; i < levels.size(); ++i) {
      auto &child = levels[i];
      auto &parent = levels[i - 1];
      ASSERT_OK(parent.vmo.create_child(ZX_VMO_CHILD_SLICE, child.offset, child.size, &child.vmo));

      // Compute the amount of space past this end of this child slice which
      // still exists in the root VMO.
      ASSERT_LE(child.size + child.offset, parent.size);
      child.size_past_end = parent.size_past_end + (parent.size - (child.size + child.offset));
    }

    // OK, now we should be ready to test each of the levels.
    for (const auto &level : levels) {
      // Make sure that we test ranges which have starting and ending points
      // entirely inside of the VMO, in the region after the VMO but inside the
      // root VMO, and entirely outside of even the root VMO.
      size_t root_end = level.size + level.size_past_end;
      for (size_t start = 0; start <= root_end; start += PAGE_SIZE) {
        for (size_t end = start + PAGE_SIZE; end <= (root_end + PAGE_SIZE); end += PAGE_SIZE) {
          zx::pmt pmt;
          uint64_t paddrs[kTestPages];
          size_t size = end - start;
          size_t expected_addrs = std::min(size / PAGE_SIZE, std::size(paddrs));

          zx_status_t expected_status =
              ((start >= level.size) || (end > level.size)) ? ZX_ERR_OUT_OF_RANGE : ZX_OK;
          EXPECT_STATUS(
              bti.pin(ZX_BTI_PERM_READ, level.vmo, start, size, paddrs, expected_addrs, &pmt),
              expected_status,
              "Op was pin offset 0x%zx size 0x%zx in VMO (offset 0x%zx size 0x%zx spe 0x%zx)",
              start, size, level.offset, level.size, level.size_past_end);

          if (pmt.is_valid()) {
            pmt.unpin();
            pmt.reset();
          }
        }
      }
    }
  }
}

TEST(VmoTestCase, DecommitChildSliceTests) {
  constexpr size_t kTestPages = 6;

  struct Level {
    Level(size_t offset_pages, size_t size_pages)
        : offset(offset_pages * PAGE_SIZE), size(size_pages * PAGE_SIZE) {}

    zx::vmo vmo;
    const size_t offset;
    const size_t size;
    size_t size_past_end = 0;
  };

  std::array levels = {
      Level{0, kTestPages},
      Level{1, kTestPages - 2},
      Level{1, kTestPages - 4},
  };

  // Create the root level of the child-slice hierarchy.
  ASSERT_OK(zx::vmo::create(levels[0].size, 0, &levels[0].vmo));

  // Now that we have our root, create each of our child slice levels.  Each
  // level will be offset by level[i].offset bytes into level[i - 1].vmo.
  for (size_t i = 1; i < levels.size(); ++i) {
    auto &child = levels[i];
    auto &parent = levels[i - 1];
    ASSERT_OK(parent.vmo.create_child(ZX_VMO_CHILD_SLICE, child.offset, child.size, &child.vmo));

    // Compute the amount of space past this end of this child slice which
    // still exists in the root VMO.
    ASSERT_LE(child.size + child.offset, parent.size);
    child.size_past_end = parent.size_past_end + (parent.size - (child.size + child.offset));
  }

  // OK, now we should be ready to test each of the levels.
  for (const auto &level : levels) {
    // Make sure that we test ranges which have starting and ending points
    // entirely inside of the VMO, in the region after the VMO but inside the
    // root VMO, and entirely outside of even the root VMO.
    size_t root_end = level.size + level.size_past_end;
    bool exercised_out_of_range = false;
    bool exercised_ok = false;
    for (size_t start = 0; start <= (root_end + PAGE_SIZE); start += PAGE_SIZE) {
      for (size_t end = start + PAGE_SIZE; end <= (root_end + (2 * PAGE_SIZE)); end += PAGE_SIZE) {
        size_t size = end - start;

        // Attempt to completely commit the root before the decommit operation.
        EXPECT_OK(levels[0].vmo.op_range(ZX_VMO_OP_COMMIT, 0, levels[0].size, nullptr, 0));

        // Now attempt to decommit our test range and check to make sure that we
        // get the expected result.  We only expect failure if our offset is out
        // of range for our child VMO.  We expect extra long sizes to be
        // silently trimmed for us.
        zx_status_t expected_status;

        if (start > level.size) {
          expected_status = ZX_ERR_OUT_OF_RANGE;
          exercised_out_of_range = true;
        } else {
          expected_status = ZX_OK;
          exercised_ok = true;
        }

        EXPECT_STATUS(
            level.vmo.op_range(ZX_VMO_OP_DECOMMIT, start, size, nullptr, 0), expected_status,
            "Decommit op was offset 0x%zx size 0x%zx in VMO (offset 0x%zx size 0x%zx spe 0x%zx)",
            start, size, level.offset, level.size, level.size_past_end);
      }
    }

    // For every level that we test, make sure we have at least one test vector
    // which expects ZX_ERR_OUT_OF_RANGE and at least one which expects ZX_OK.
    EXPECT_TRUE(exercised_out_of_range);
    EXPECT_TRUE(exercised_ok);
  }
}

TEST(VmoTestCase, MetadataBytes) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

  // Until we use the VMO we expect metadata to be zero
  zx_info_vmo_t info;
  EXPECT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));

  EXPECT_EQ(info.metadata_bytes, 0);

  // This is a paged VMO so once we do something to commit a page we expect non-zero metadata.
  EXPECT_OK(vmo.write(&info, 0, sizeof(info)));
  EXPECT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));

  EXPECT_NE(info.metadata_bytes, 0);
}

TEST(VmoTestCase, V1Info) {
  zx::vmo vmo;
  EXPECT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

  // Check that the old info can be queried and makes sense
  zx_info_vmo_v1_t v1info;
  zx_info_vmo_t info;
  EXPECT_OK(vmo.get_info(ZX_INFO_VMO_V1, &v1info, sizeof(v1info), nullptr, nullptr));
  EXPECT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));

  // Check a subset of the fields that we expect to be stable and non-racy between the two different
  // get_info invocations.
  EXPECT_EQ(v1info.koid, info.koid);
  EXPECT_EQ(v1info.size_bytes, info.size_bytes);
  EXPECT_EQ(v1info.parent_koid, info.parent_koid);
  EXPECT_EQ(v1info.num_children, info.num_children);
  EXPECT_EQ(v1info.num_mappings, info.num_mappings);
  EXPECT_EQ(v1info.share_count, info.share_count);
  EXPECT_EQ(v1info.flags, info.flags);
  EXPECT_EQ(v1info.handle_rights, info.handle_rights);
  EXPECT_EQ(v1info.cache_policy, info.cache_policy);
}

}  // namespace
