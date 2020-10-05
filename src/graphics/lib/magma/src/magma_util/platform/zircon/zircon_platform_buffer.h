// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_BUFFER_H
#define ZIRCON_PLATFORM_BUFFER_H

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>  // PAGE_SIZE

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_object.h"

namespace magma {

class ZirconPlatformBuffer : public PlatformBuffer {
 public:
  class MappingAddressRange : public PlatformBuffer::MappingAddressRange {
   public:
    MappingAddressRange(zx::vmar vmar) : vmar_(std::move(vmar)) {}

    bool is_root() const { return !vmar_.is_valid(); }

    zx::unowned<zx::vmar> get() const {
      return vmar_ ? zx::unowned_vmar(vmar_.get()) : zx::vmar::root_self();
    }

    uint64_t Length() override;
    uint64_t Base() override;

   private:
    zx::vmar vmar_;
    DISALLOW_COPY_AND_ASSIGN(MappingAddressRange);
  };

  ZirconPlatformBuffer(zx::vmo vmo, uint64_t size) : vmo_(std::move(vmo)), size_(size) {
    DLOG("ZirconPlatformBuffer ctor size %ld vmo 0x%x", size, vmo_.get());
    DASSERT(magma::is_page_aligned(size));

    bool success = PlatformObject::IdFromHandle(vmo_.get(), &koid_);
    DASSERT(success);
  }

  ~ZirconPlatformBuffer() override {
    if (map_count_ > 0)
      vmar_unmap();
  }

  // PlatformBuffer implementation
  uint64_t size() const override { return size_; }

  uint64_t id() const override { return koid_; }

  zx_handle_t handle() const { return vmo_.get(); }

  bool duplicate_handle(uint32_t* handle_out) const override {
    zx::vmo duplicate;
    zx_status_t status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
    if (status < 0)
      return DRETF(false, "zx_handle_duplicate failed");
    *handle_out = duplicate.release();
    return true;
  }

  // Creates a duplicate handle whose lifetime can be tracked with HasChildren.
  bool CreateChild(uint32_t* handle_out) override;

  // Returns true if one or more child buffers exist.
  bool HasChildren() const override;

  // PlatformBuffer implementation
  bool CommitPages(uint64_t start_page_index, uint64_t page_count) const override;
  bool MapCpu(void** addr_out, uintptr_t alignment) override;
  bool MapCpuConstrained(void** va_out, uint64_t length, uint64_t upper_limit,
                         uint64_t alignment) override;
  bool UnmapCpu() override;
  bool MapAtCpuAddr(uint64_t addr, uint64_t offset, uint64_t length) override;
  bool MapCpuWithFlags(uint64_t offset, uint64_t length, uint64_t flags,
                       std::unique_ptr<Mapping>* mapping_out) override;
  bool SetPadding(uint64_t padding) override;

  bool CleanCache(uint64_t offset, uint64_t size, bool invalidate) override;
  bool SetCachePolicy(magma_cache_policy_t cache_policy) override;
  magma_status_t GetCachePolicy(magma_cache_policy_t* cache_policy_out) override;
  magma_status_t GetIsMappable(magma_bool_t* is_mappable_out) override;
  magma::Status SetMappingAddressRange(
      std::unique_ptr<PlatformBuffer::MappingAddressRange> address_range) override;
  bool Read(void* buffer, uint64_t offset, uint64_t length) override;
  bool Write(const void* buffer, uint64_t offset, uint64_t length) override;
  bool SetName(const char* name) override;

  uint64_t num_pages() { return size_ / PAGE_SIZE; }

 private:
  zx_status_t vmar_unmap() {
    zx_status_t status = vmar_.destroy();
    vmar_.reset();

    if (status == ZX_OK)
      virt_addr_ = nullptr;
    return status;
  }

  zx::vmo vmo_;
  zx::vmar vmar_;
  uint64_t size_;
  uint64_t padding_size_ = 0;
  uint64_t koid_;
  void* virt_addr_{};
  uint32_t map_count_ = 0;
  std::shared_ptr<MappingAddressRange> parent_vmar_ =
      std::make_shared<MappingAddressRange>(zx::vmar());
};

}  // namespace magma

#endif
