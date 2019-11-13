// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_
#define ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

namespace vmo_test {

static inline void VmoWrite(const zx::vmo& vmo, uint32_t data, uint64_t offset = 0) {
  zx_status_t status = vmo.write(static_cast<void*>(&data), offset, sizeof(data));
  ASSERT_OK(status, "write failed");
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
  status = zx::vmo::create(page_count * ZX_PAGE_SIZE, ZX_VMO_RESIZABLE, vmo);
  ASSERT_OK(status, "create failed");
  for (unsigned i = 0; i < page_count; i++) {
    ASSERT_NO_FATAL_FAILURES(VmoWrite(*vmo, i + 1, i * ZX_PAGE_SIZE));
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
        zx::vmar::root_self()->map(0, vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &addr_);
    len_ = len;
    return status;
  }

  uint32_t* ptr() { return reinterpret_cast<uint32_t*>(addr_); }
  uint8_t* bytes() { return reinterpret_cast<uint8_t*>(addr_); }

 private:
  uint64_t addr_ = 0;
  size_t len_ = 0;
};

}  // namespace vmo_test

#endif  // ZIRCON_SYSTEM_UTEST_CORE_VMO_HELPERS_H_
