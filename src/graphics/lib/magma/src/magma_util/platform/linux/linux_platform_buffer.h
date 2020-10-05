// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_BUFFER_H
#define LINUX_PLATFORM_BUFFER_H

#include "magma_util/macros.h"
#include "platform_buffer.h"

namespace magma {

// Linux implementation of a PlatformBuffer
class LinuxPlatformBuffer : public PlatformBuffer {
 public:
  class MappingAddressRange : public PlatformBuffer::MappingAddressRange {
   public:
    MappingAddressRange() {}

    uint64_t Length() override;
    uint64_t Base() override;

   private:
    DISALLOW_COPY_AND_ASSIGN(MappingAddressRange);
  };

  LinuxPlatformBuffer(int memfd, uint64_t id, uint64_t size)
      : memfd_(memfd), id_(id), size_(size) {}

  int memfd() { return memfd_; }

  // PlatformBuffer implementation
  uint64_t size() const override { return size_; }

  uint64_t id() const override { return id_; }

  bool duplicate_handle(uint32_t* handle_out) const override;
  bool CommitPages(uint64_t start_page_index, uint64_t page_count) const override;
  bool MapCpu(void** addr_out, uintptr_t alignment) override;
  bool UnmapCpu() override;
  bool MapAtCpuAddr(uint64_t addr, uint64_t offset, uint64_t length) override;
  bool MapCpuWithFlags(uint64_t offset, uint64_t length, uint64_t flags,
                       std::unique_ptr<Mapping>* mapping_out) override;

  bool CleanCache(uint64_t offset, uint64_t size, bool invalidate) override;
  bool SetCachePolicy(magma_cache_policy_t cache_policy) override;
  magma_status_t GetCachePolicy(magma_cache_policy_t* cache_policy_out) override;
  magma_status_t GetIsMappable(magma_bool_t* is_mappable_out) override;
  magma::Status SetMappingAddressRange(
      std::unique_ptr<PlatformBuffer::MappingAddressRange> address_range) override;
  bool Read(void* buffer, uint64_t offset, uint64_t length) override;
  bool Write(const void* buffer, uint64_t offset, uint64_t length) override;
  bool SetPadding(uint64_t padding_size) override { return false; }

 private:
  int memfd_;
  uint64_t id_;
  uint64_t size_;
  void* virt_addr_ = nullptr;
  uint32_t map_count_ = 0;
};

}  // namespace magma

#endif
