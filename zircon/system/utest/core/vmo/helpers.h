// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_
#define ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_

#include <lib/fit/defer.h>
#include <lib/zx/bti.h>
#include <lib/zx/status.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

namespace vmo_test {

static inline void VmoWrite(const zx::vmo& vmo, uint32_t data, uint64_t offset = 0) {
  zx_status_t status = vmo.write(static_cast<void*>(&data), offset, sizeof(data));
  ASSERT_OK(status, "write failed");
}

static inline uint32_t VmoRead(const zx::vmo& vmo, uint64_t offset = 0) {
  uint32_t val = 0;
  zx_status_t status = vmo.read(&val, offset, sizeof(val));
  EXPECT_OK(status, "read failed");
  return val;
}

static inline void VmoCheck(const zx::vmo& vmo, uint32_t expected, uint64_t offset = 0) {
  uint32_t data;
  zx_status_t status = vmo.read(static_cast<void*>(&data), offset, sizeof(data));
  ASSERT_OK(status, "read failed");
  ASSERT_EQ(expected, data);
}

// Creates a vmo with |page_count| pages and writes (page_index + 1) to each page.
static inline void InitPageTaggedVmo(uint32_t page_count, zx::vmo* vmo) {
  zx_status_t status;
  status = zx::vmo::create(page_count * zx_system_get_page_size(), ZX_VMO_RESIZABLE, vmo);
  ASSERT_OK(status, "create failed");
  for (unsigned i = 0; i < page_count; i++) {
    ASSERT_NO_FATAL_FAILURE(VmoWrite(*vmo, i + 1, i * zx_system_get_page_size()));
  }
}

static inline size_t VmoNumChildren(const zx::vmo& vmo) {
  zx_info_vmo_t info;
  if (vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return UINT64_MAX;
  }
  return info.num_children;
}

static inline size_t VmoCommittedBytes(const zx::vmo& vmo) {
  zx_info_vmo_t info;
  if (vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return UINT64_MAX;
  }
  return info.committed_bytes;
}

// Create a fit::defer which will check a BTI to make certain that it has no
// pinned or quarantined pages when it goes out of scope, and fail the test if
// it does.
static inline auto CreateDeferredBtiCheck(const zx::bti& bti) {
  return fit::defer([&bti]() {
    if (bti.is_valid()) {
      zx_info_bti_t info;
      ASSERT_OK(bti.get_info(ZX_INFO_BTI, &info, sizeof(info), nullptr, nullptr));
      EXPECT_EQ(0, info.pmo_count);
      EXPECT_EQ(0, info.quarantine_count);
    }
  });
}

// Simple class for managing vmo mappings w/o any external dependencies.
class Mapping {
 public:
  ~Mapping() {
    if (addr_) {
      ZX_ASSERT(zx::vmar::root_self()->unmap(addr_, len_) == ZX_OK);
    }
  }

  zx_status_t Init(const zx::vmo& vmo, size_t len) {
    zx_status_t status =
        zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, len, &addr_);
    len_ = len;
    return status;
  }

  uint32_t* ptr() { return reinterpret_cast<uint32_t*>(addr_); }
  uint8_t* bytes() { return reinterpret_cast<uint8_t*>(addr_); }

 private:
  uint64_t addr_ = 0;
  size_t len_ = 0;
};

// A simple struct and function which can be used to attempt to fetch a VMO
// created using zx_vmo_create_physical from a region which should have been
// reserved using the kernel.test.ram.reserve boot option.
struct PhysVmo {
  uintptr_t addr = 0;
  size_t size = 0;
  zx::vmo vmo;
};

// Create and return a physical VMO from the reserved regions of RAM.  |size|
// indicates the desired size of the VMO, or 0 to fetch the entire reserved
// region of RAM, whatever its size might be.
zx::result<PhysVmo> GetTestPhysVmo(size_t size = 0);

zx::bti CreateNamedBti(const zx::iommu& fake_iommu, uint32_t options, uint64_t bti_id,
                       const char* name);

}  // namespace vmo_test

#endif  // ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_
