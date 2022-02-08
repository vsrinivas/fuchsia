// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-object/object.h>
#include <lib/fake-resource/resource.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <array>
#include <climits>  // PAGE_SIZE
#include <utility>

#include <zxtest/zxtest.h>

namespace {

zx::resource root_resource;

class FakeResource : public zxtest::Test {
 public:
  static void SetUpTestCase() {
    ASSERT_OK(fake_root_resource_create(root_resource.reset_and_get_address()));
  }

  static void TearDownTestCase() { root_resource.reset(); }

 private:
  zx::resource root_;
};

bool validate_resource_info(zx::resource& res, zx_paddr_t base, size_t size, zx_rsrc_kind_t kind,
                            const char* name) {
  zx_info_resource_t info;
  zx_status_t st = res.get_info(ZX_INFO_RESOURCE, &info, sizeof(info), nullptr, nullptr);
  if (st != ZX_OK) {
    return false;
  }

  return (info.kind == kind && info.base == base && info.size == size &&
          strncmp(info.name, name, sizeof(info.name)) == 0);
}

TEST_F(FakeResource, ChildBoundsTest) {
  std::array<char, ZX_MAX_NAME_LEN> parent_name = {"parent"};
  std::array<char, ZX_MAX_NAME_LEN> child_name = {"child"};
  // Create a parent resource from |4096-8192|
  const uintptr_t parent_base = PAGE_SIZE;
  const size_t parent_size = PAGE_SIZE;
  zx::resource parent;
  ASSERT_OK(zx::resource::create(root_resource, ZX_RSRC_KIND_MMIO, parent_base, parent_size,
                                 parent_name.data(), parent_name.size(), &parent));
  ASSERT_TRUE(validate_resource_info(parent, parent_base, parent_size, ZX_RSRC_KIND_MMIO,
                                     parent_name.data()));
  zx::resource child;
  // Same span.
  ASSERT_OK(zx::resource::create(parent, ZX_RSRC_KIND_MMIO, parent_base, parent_size,
                                 child_name.data(), child_name.size(), &child));
  // Subset of parent
  ASSERT_OK(zx::resource::create(parent, ZX_RSRC_KIND_MMIO, parent_base + 1024, 1024,
                                 child_name.data(), child_name.size(), &child));
  // Superset of parent.
  ASSERT_NOT_OK(zx::resource::create(parent, ZX_RSRC_KIND_MMIO, parent_base - 2048,
                                     parent_size + 4096, child_name.data(), child_name.size(),
                                     &child));
  // Before parent base.
  ASSERT_NOT_OK(zx::resource::create(parent, ZX_RSRC_KIND_MMIO, parent_base - 2048, parent_size,
                                     child_name.data(), child_name.size(), &child));
  // Past parent length.
  ASSERT_NOT_OK(zx::resource::create(parent, ZX_RSRC_KIND_MMIO, parent_base + 2048, parent_size,
                                     child_name.data(), child_name.size(), &child));
}

TEST_F(FakeResource, ExclusiveBoundsTest) {
  std::array<char, ZX_MAX_NAME_LEN> first_name = {"first"};
  std::array<char, ZX_MAX_NAME_LEN> second_name = {"second"};
  // Create a first resource from |4096-20480|
  const uintptr_t first_base = PAGE_SIZE;
  const uint64_t first_size = static_cast<const uint64_t>(zx_system_get_page_size()) * 4;
  uint32_t flags = ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE;
  zx::resource first, second;
  ASSERT_OK(zx::resource::create(root_resource, flags, first_base, first_size, first_name.data(),
                                 first_name.size(), &first));
  ASSERT_TRUE(validate_resource_info(first, first_base, first_size, ZX_RSRC_EXTRACT_KIND(flags),
                                     first_name.data()));
  // Same span.
  {
    ASSERT_NOT_OK(zx::resource::create(root_resource, flags, first_base, first_size,
                                       second_name.data(), second_name.size(), &second));
  }
  // Subset of first
  ASSERT_NOT_OK(zx::resource::create(root_resource, flags, first_base + PAGE_SIZE, PAGE_SIZE,
                                     second_name.data(), second_name.size(), &second));
  // Superset of first.
  ASSERT_NOT_OK(zx::resource::create(root_resource, flags, first_base - PAGE_SIZE,
                                     first_size + PAGE_SIZE, second_name.data(), second_name.size(),
                                     &second));
  // Before first base.
  ASSERT_NOT_OK(zx::resource::create(root_resource, flags, first_base - PAGE_SIZE, first_size,
                                     second_name.data(), second_name.size(), &second));
  // Past first length.
  ASSERT_NOT_OK(zx::resource::create(root_resource, flags, first_base + PAGE_SIZE, first_size,
                                     second_name.data(), second_name.size(), &second));
  // Separate region entirely
  ASSERT_OK(zx::resource::create(root_resource, flags, first_base + first_size + PAGE_SIZE,
                                 PAGE_SIZE, second_name.data(), second_name.size(), &second));
}

TEST_F(FakeResource, ExclusiveNewAfterExisting) {
  std::array<char, ZX_MAX_NAME_LEN> first_name = {"first"};
  std::array<char, ZX_MAX_NAME_LEN> second_name = {"second"};
  uintptr_t first_base = 0x1000;
  uintptr_t size = 0x4000;
  uint32_t flags = ZX_RSRC_KIND_MMIO | ZX_RSRC_FLAG_EXCLUSIVE;
  zx::resource first, second;
  ASSERT_OK(zx::resource::create(root_resource, flags, first_base, size, first_name.data(),
                                 first_name.size(), &first));
  ASSERT_OK(zx::resource::create(root_resource, flags, first_base + size, size, second_name.data(),
                                 second_name.size(), &second));
}

TEST_F(FakeResource, IOPortTest) {
  zx::resource io_child;
  zx::resource null_child;
  zx::resource mmio_child;
  std::array<char, ZX_MAX_NAME_LEN> child_name = {"child"};
  ASSERT_OK(zx::resource::create(root_resource, ZX_RSRC_KIND_IOPORT, 128, 128, child_name.data(),
                                 child_name.size(), &io_child));
  ASSERT_OK(zx::resource::create(root_resource, ZX_RSRC_KIND_IOPORT, 0, 0, child_name.data(),
                                 child_name.size(), &null_child));
  ASSERT_OK(zx::resource::create(root_resource, ZX_RSRC_KIND_MMIO, 128, 128, child_name.data(),
                                 child_name.size(), &mmio_child));
  zx_info_resource_t info;
  ASSERT_OK(
      zx_object_get_info(io_child.get(), ZX_INFO_RESOURCE, &info, sizeof(info), nullptr, nullptr));
  // Within the span
  ASSERT_OK(zx_ioports_request(io_child.get(), static_cast<uint16_t>(info.base + 64), 32));
  ASSERT_OK(zx_ioports_release(io_child.get(), static_cast<uint16_t>(info.base + 64), 32));
  // MMIO resources should not work
  ASSERT_NOT_OK(zx_ioports_request(mmio_child.get(), 64, 32));
  ASSERT_NOT_OK(zx_ioports_release(mmio_child.get(), 64, 32));
  // IOPort resources with no allowable window shouldn't work either
  ASSERT_NOT_OK(zx_ioports_request(null_child.get(), 512, 512));
  ASSERT_NOT_OK(zx_ioports_release(null_child.get(), 512, 512));
}

TEST_F(FakeResource, VmoTest) {
  const uint64_t MAP_LEN = 64u;
  zx::resource child;
  std::array<char, ZX_MAX_NAME_LEN> child_name = {"child"};
  ASSERT_OK(zx::resource::create(root_resource, ZX_RSRC_KIND_MMIO, 0, PAGE_SIZE, child_name.data(),
                                 child_name.size(), &child));
  ASSERT_TRUE(validate_resource_info(child, 0, PAGE_SIZE, ZX_RSRC_KIND_MMIO, child_name.data()));
  zx::vmo vmo;
  uintptr_t vaddr;
  ASSERT_OK(zx::vmo::create_physical(child, 0, PAGE_SIZE, &vmo));
  ASSERT_OK(vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE));
  ASSERT_OK(
      zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, MAP_LEN, &vaddr));

  // Perform some operations on the fake physical VMO we created to make sure
  // nothing was screwed up in the chain.
  std::array<uint8_t, MAP_LEN> buf = {};
  memset(buf.data(), 0xA5, MAP_LEN);
  memcpy(reinterpret_cast<uint8_t*>(vaddr), buf.data(), MAP_LEN);
  ASSERT_BYTES_EQ(reinterpret_cast<uint8_t*>(vaddr), buf.data(), MAP_LEN);
  ASSERT_OK(zx::vmar::root_self()->unmap(vaddr, MAP_LEN));
}

}  // namespace
