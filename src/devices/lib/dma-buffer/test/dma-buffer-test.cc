// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/dma-buffer/buffer.h>
#include <lib/fake-object/object.h>

#include <map>

#include <zxtest/zxtest.h>

namespace dma_buffer {

const zx::bti kFakeBti(42);

struct VmoMetadata {
  size_t size = 0;
  uint32_t alignment_log2 = 0;
  zx_handle_t bti_handle = ZX_HANDLE_INVALID;
  uint32_t cache_policy = 0;
  zx_paddr_t start_phys = 0;
  void* virt = nullptr;
  bool contiguous = false;
};

struct SyscallState {
  std::map<zx_handle_t, VmoMetadata> vmos;
  uint64_t current_phys = 0;
};

static SyscallState syscall_state;

extern "C" {
zx_status_t zx_vmo_create_contiguous(zx_handle_t bti_handle, size_t size, uint32_t alignment_log2,
                                     zx_handle_t* out) {
  zx_status_t status = zx_vmo_create(size, 0, out);
  if (status != ZX_OK) {
    return status;
  }
  VmoMetadata meta;
  meta.alignment_log2 = alignment_log2;
  meta.bti_handle = bti_handle;
  meta.size = size;
  syscall_state.vmos[*out] = meta;
  return ZX_OK;
}

zx_status_t zx_vmo_create(uint64_t size, uint32_t options, zx_handle_t* out) {
  zx_status_t status = REAL_SYSCALL(zx_vmo_create)(size, options, out);
  if (status != ZX_OK) {
    return status;
  }
  VmoMetadata meta;
  meta.size = size;
  syscall_state.vmos[*out] = meta;
  return ZX_OK;
}

zx_status_t zx_vmar_map(zx_handle_t handle, zx_vm_option_t options, uint64_t vmar_offset,
                        zx_handle_t vmo, uint64_t vmo_offset, uint64_t len,
                        zx_vaddr_t* mapped_addr) {
  auto record = syscall_state.vmos.find(vmo);
  zx_status_t status =
      REAL_SYSCALL(zx_vmar_map)(handle, options, vmar_offset, vmo, vmo_offset, len, mapped_addr);
  if (record != syscall_state.vmos.end()) {
    record->second.virt = reinterpret_cast<void*>(*mapped_addr);
  }
  return status;
}

zx_status_t zx_vmo_set_cache_policy(zx_handle_t handle, uint32_t cache_policy) {
  syscall_state.vmos[handle].cache_policy = cache_policy;
  return ZX_OK;
}

zx_status_t zx_bti_pin(zx_handle_t bti_handle, uint32_t options, zx_handle_t vmo, uint64_t offset,
                       uint64_t size, zx_paddr_t* addrs, size_t addrs_count, zx_handle_t* out) {
  if (bti_handle != kFakeBti.get()) {
    return ZX_ERR_BAD_HANDLE;
  }
  syscall_state.vmos[vmo].start_phys = syscall_state.current_phys;
  *addrs = syscall_state.current_phys;
  syscall_state.current_phys += syscall_state.vmos[vmo].size;
  *out = ZX_HANDLE_INVALID;
  return ZX_OK;
}

zx_status_t zx_handle_close(zx_handle_t handle) {
  auto record = syscall_state.vmos.find(handle);
  if (record == syscall_state.vmos.end()) {
    return REAL_SYSCALL(zx_handle_close)(handle);
  }
  syscall_state.vmos.erase(record);
  return ZX_OK;
}
}

TEST(DmaBufferTests, InitWithCacheEnabled) {
  std::optional<ContiguousBuffer> buffer;
  ASSERT_OK(ContiguousBuffer::Create(kFakeBti, ZX_PAGE_SIZE * 4, 2, &buffer));
  auto& state = syscall_state.vmos.begin()->second;
  ASSERT_EQ(state.alignment_log2, 2);
  ASSERT_EQ(state.bti_handle, kFakeBti.get());
  ASSERT_EQ(state.cache_policy, 0);
  ASSERT_EQ(state.size, ZX_PAGE_SIZE * 4);
  ASSERT_EQ(buffer->virt(), state.virt);
  ASSERT_EQ(buffer->size(), state.size);
  ASSERT_EQ(buffer->phys(), state.start_phys);
}

TEST(DmaBufferTests, InitWithCacheDisabled) {
  std::optional<PagedBuffer> buffer;
  ASSERT_OK(PagedBuffer::Create(kFakeBti, ZX_PAGE_SIZE, false, &buffer));
  auto& state = syscall_state.vmos.begin()->second;
  ASSERT_EQ(state.alignment_log2, 0);
  ASSERT_EQ(state.cache_policy, ZX_CACHE_POLICY_UNCACHED);
  ASSERT_EQ(state.size, ZX_PAGE_SIZE);
  ASSERT_EQ(buffer->virt(), state.virt);
  ASSERT_EQ(buffer->size(), state.size);
  ASSERT_EQ(buffer->phys()[0], state.start_phys);
}

TEST(DmaBufferTests, InitCachedMultiPageBuffer) {
  std::optional<ContiguousBuffer> buffer;
  ASSERT_OK(ContiguousBuffer::Create(kFakeBti, ZX_PAGE_SIZE * 4, 0, &buffer));
  auto& state = syscall_state.vmos.begin()->second;
  ASSERT_EQ(state.alignment_log2, 0);
  ASSERT_EQ(state.cache_policy, 0);
  ASSERT_EQ(state.bti_handle, kFakeBti.get());
  ASSERT_EQ(state.size, ZX_PAGE_SIZE * 4);
  ASSERT_EQ(buffer->virt(), state.virt);
  ASSERT_EQ(buffer->size(), state.size);
  ASSERT_EQ(buffer->phys(), state.start_phys);
}

}  // namespace dma_buffer
