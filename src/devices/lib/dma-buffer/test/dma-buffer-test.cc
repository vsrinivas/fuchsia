// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/dma-buffer/buffer.h>
#include <lib/fake-object/object.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <map>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

namespace {

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

static bool unpinned = false;

class VmoWrapper : public fake_object::Object {
 public:
  explicit VmoWrapper(zx::vmo vmo) : vmo_(std::move(vmo)) {}
  virtual fake_object::HandleType type() const final { return fake_object::HandleType::CUSTOM; }
  zx::unowned_vmo vmo() { return vmo_.borrow(); }
  VmoMetadata& metadata() { return metadata_; }

 private:
  zx::vmo vmo_;
  VmoMetadata metadata_ = {};
};

extern "C" {
zx_status_t zx_vmo_create_contiguous(zx_handle_t bti_handle, size_t size, uint32_t alignment_log2,
                                     zx_handle_t* out) {
  zx::vmo vmo = {};
  zx_status_t status = REAL_SYSCALL(zx_vmo_create)(size, 0, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  auto vmo_wrapper = fbl::AdoptRef(new VmoWrapper(std::move(vmo)));
  vmo_wrapper->metadata().alignment_log2 = alignment_log2;
  vmo_wrapper->metadata().bti_handle = bti_handle;
  vmo_wrapper->metadata().size = size;
  zx::status add_res = fake_object::FakeHandleTable().Add(std::move(vmo_wrapper));
  if (add_res.is_ok()) {
    *out = add_res.value();
  }
  return add_res.status_value();
}

zx_status_t zx_vmo_create(uint64_t size, uint32_t options, zx_handle_t* out) {
  zx::vmo vmo = {};
  zx_status_t status = REAL_SYSCALL(zx_vmo_create)(size, 0, vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  auto vmo_wrapper = fbl::AdoptRef(new VmoWrapper(std::move(vmo)));
  vmo_wrapper->metadata().size = size;
  zx::status add_res = fake_object::FakeHandleTable().Add(std::move(vmo_wrapper));
  if (add_res.is_ok()) {
    *out = add_res.value();
  }
  return add_res.status_value();
}

zx_status_t zx_vmar_map(zx_handle_t vmar_handle, zx_vm_option_t options, uint64_t vmar_offset,
                        zx_handle_t vmo_handle, uint64_t vmo_offset, uint64_t len,
                        zx_vaddr_t* mapped_addr) {
  zx::status get_res = fake_object::FakeHandleTable().Get(vmo_handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  auto vmo = fbl::RefPtr<VmoWrapper>::Downcast(std::move(get_res.value()));

  zx_status_t status = REAL_SYSCALL(zx_vmar_map)(vmar_handle, options, vmar_offset,
                                                 vmo->vmo()->get(), vmo_offset, len, mapped_addr);
  if (status == ZX_OK) {
    vmo->metadata().virt = reinterpret_cast<void*>(*mapped_addr);
  }
  return status;
}

zx_status_t zx_vmo_set_cache_policy(zx_handle_t handle, uint32_t cache_policy) {
  zx::status get_res = fake_object::FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  auto vmo = fbl::RefPtr<VmoWrapper>::Downcast(std::move(get_res.value()));
  vmo->metadata().cache_policy = cache_policy;
  return ZX_OK;
}

zx_status_t zx_bti_pin(zx_handle_t bti_handle, uint32_t options, zx_handle_t vmo_handle,
                       uint64_t offset, uint64_t size, zx_paddr_t* addrs, size_t addrs_count,
                       zx_handle_t* out) {
  static uint64_t current_phys = 0;
  static fbl::Mutex phys_lock;

  if (bti_handle != kFakeBti.get()) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx::status get_res = fake_object::FakeHandleTable().Get(vmo_handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  auto vmo = fbl::RefPtr<VmoWrapper>::Downcast(std::move(get_res.value()));

  fbl::AutoLock lock(&phys_lock);
  vmo->metadata().start_phys = current_phys;
  *addrs = current_phys;
  current_phys += vmo->metadata().size;
  *out = ZX_HANDLE_INVALID;
  return ZX_OK;
}

zx_status_t zx_pmt_unpin(zx_handle_t handle) {
  if (handle == ZX_HANDLE_INVALID) {
    unpinned = true;
  }
  return ZX_OK;
}

}  // extern "C"

}  // namespace

namespace dma_buffer {
TEST(DmaBufferTests, InitWithCacheEnabled) {
  unpinned = false;
  {
    std::unique_ptr<ContiguousBuffer> buffer;
    const size_t size = ZX_PAGE_SIZE * 4;
    const size_t alignment = 2;
    auto factory = CreateBufferFactory();
    ASSERT_OK(factory->CreateContiguous(kFakeBti, size, alignment, &buffer));
    auto test_f = [&buffer](fake_object::Object* obj) -> bool {
      auto vmo = static_cast<VmoWrapper*>(obj);
      ZX_ASSERT(vmo->metadata().alignment_log2 == alignment);
      ZX_ASSERT(vmo->metadata().bti_handle == kFakeBti.get());
      ZX_ASSERT(vmo->metadata().cache_policy == 0);
      ZX_ASSERT(vmo->metadata().size == size);
      ZX_ASSERT(buffer->virt() == vmo->metadata().virt);
      ZX_ASSERT(buffer->size() == vmo->metadata().size);
      ZX_ASSERT(buffer->phys() == vmo->metadata().start_phys);
      return false;
    };
    fake_object::FakeHandleTable().ForEach(fake_object::HandleType::CUSTOM, test_f);
  }
  ASSERT_TRUE(unpinned);
}

TEST(DmaBufferTests, InitWithCacheDisabled) {
  unpinned = false;
  {
    std::unique_ptr<PagedBuffer> buffer;
    auto factory = CreateBufferFactory();
    ASSERT_OK(factory->CreatePaged(kFakeBti, ZX_PAGE_SIZE, false, &buffer));
    auto test_f = [&buffer](fake_object::Object* object) -> bool {
      auto vmo = static_cast<VmoWrapper*>(object);
      ZX_ASSERT(vmo->metadata().alignment_log2 == 0);
      ZX_ASSERT(vmo->metadata().cache_policy == ZX_CACHE_POLICY_UNCACHED_DEVICE);
      ZX_ASSERT(vmo->metadata().size == ZX_PAGE_SIZE);
      ZX_ASSERT(buffer->virt() == vmo->metadata().virt);
      ZX_ASSERT(buffer->size() == vmo->metadata().size);
      ZX_ASSERT(buffer->phys()[0] == vmo->metadata().start_phys);
      return false;
    };
    fake_object::FakeHandleTable().ForEach(fake_object::HandleType::CUSTOM, test_f);
  }
  ASSERT_TRUE(unpinned);
}

TEST(DmaBufferTests, InitCachedMultiPageBuffer) {
  unpinned = false;
  {
    std::unique_ptr<ContiguousBuffer> buffer;
    auto factory = CreateBufferFactory();
    ASSERT_OK(factory->CreateContiguous(kFakeBti, ZX_PAGE_SIZE * 4, 0, &buffer));
    auto test_f = [&buffer](fake_object::Object* object) -> bool {
      auto vmo = static_cast<VmoWrapper*>(object);
      ZX_ASSERT(vmo->metadata().alignment_log2 == 0);
      ZX_ASSERT(vmo->metadata().cache_policy == 0);
      ZX_ASSERT(vmo->metadata().bti_handle == kFakeBti.get());
      ZX_ASSERT(vmo->metadata().size == ZX_PAGE_SIZE * 4);
      ZX_ASSERT(buffer->virt() == vmo->metadata().virt);
      ZX_ASSERT(buffer->size() == vmo->metadata().size);
      ZX_ASSERT(buffer->phys() == vmo->metadata().start_phys);
      return false;
    };
    fake_object::FakeHandleTable().ForEach(fake_object::HandleType::CUSTOM, test_f);
  }
  ASSERT_TRUE(unpinned);
}

}  // namespace dma_buffer
