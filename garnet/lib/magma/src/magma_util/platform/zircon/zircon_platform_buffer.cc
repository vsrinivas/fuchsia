// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_buffer.h"

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>  // PAGE_SIZE

#include <map>
#include <vector>

#include "platform_trace.h"
#include "zircon_platform_handle.h"

namespace magma {

std::unique_ptr<PlatformBuffer::MappingAddressRange> PlatformBuffer::MappingAddressRange::Create(
    std::unique_ptr<magma::PlatformHandle> handle) {
  return std::make_unique<ZirconPlatformBuffer::MappingAddressRange>(
      handle ? zx::vmar(handle->release()) : zx::vmar());
}

uint64_t ZirconPlatformBuffer::MappingAddressRange::Length() {
  zx_info_vmar_t info;
  get()->get_info(ZX_INFO_VMAR, &info, sizeof(info), nullptr, nullptr);
  return info.len;
}

uint64_t ZirconPlatformBuffer::MappingAddressRange::Base() {
  zx_info_vmar_t info;
  get()->get_info(ZX_INFO_VMAR, &info, sizeof(info), nullptr, nullptr);
  return info.base;
}

class ZirconPlatformBufferMapping : public PlatformBuffer::Mapping {
 public:
  ZirconPlatformBufferMapping(
      uint64_t addr, uint64_t size,
      std::shared_ptr<ZirconPlatformBuffer::MappingAddressRange> parent_vmar)
      : addr_(addr), size_(size), parent_vmar_(std::move(parent_vmar)) {
    DASSERT(parent_vmar_);
  }

  ~ZirconPlatformBufferMapping() override { parent_vmar_->get()->unmap(addr_, size_); }

  void* address() override { return reinterpret_cast<void*>(addr_); }

 private:
  uint64_t addr_;
  uint64_t size_;
  std::shared_ptr<ZirconPlatformBuffer::MappingAddressRange> parent_vmar_;
};

bool ZirconPlatformBuffer::MapCpuWithFlags(uint64_t offset, uint64_t length, uint64_t flags,
                                           std::unique_ptr<Mapping>* mapping_out) {
  if (!magma::is_page_aligned(offset))
    return DRETF(false, "offset %lx isn't page aligned", offset);
  if (!magma::is_page_aligned(length))
    return DRETF(false, "length %lx isn't page aligned", length);
  if (offset + length > size())
    return DRETF(false, "offset %lx + length %lx > size %lx", offset, length, size());

  uint32_t map_flags = 0;
  if (flags & kMapRead)
    map_flags |= ZX_VM_PERM_READ;
  if (flags & kMapWrite)
    map_flags |= ZX_VM_PERM_WRITE;
  uintptr_t ptr;
  zx_status_t status = parent_vmar_->get()->map(0, vmo_, offset, length, flags, &ptr);
  if (status != ZX_OK) {
    return DRETF(false, "Failed to map: %d", status);
  }
  *mapping_out = std::make_unique<ZirconPlatformBufferMapping>(ptr, length, parent_vmar_);
  return true;
}

bool ZirconPlatformBuffer::MapAtCpuAddr(uint64_t addr, uint64_t offset, uint64_t length) {
  if (!magma::is_page_aligned(addr))
    return DRETF(false, "addr %lx isn't page aligned", addr);
  if (!magma::is_page_aligned(offset))
    return DRETF(false, "offset %lx isn't page aligned", offset);
  if (!magma::is_page_aligned(length))
    return DRETF(false, "length %lx isn't page aligned", length);
  if (offset + length > size())
    return DRETF(false, "offset %lx + length %lx > size %lx", offset, length, size());
  if (map_count_ > 0)
    return DRETF(false, "buffer is already mapped");
  DASSERT(!vmar_);

  uint64_t vmar_base = parent_vmar_->Base();
  if (addr < vmar_base)
    return DRETF(false, "addr %lx below vmar base %lx", addr, vmar_base);

  uint64_t child_addr;
  zx::vmar child_vmar;
  zx_status_t status = parent_vmar_->get()->allocate(
      addr - vmar_base, length,
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_SPECIFIC,
      &child_vmar, &child_addr);
  // This may happen often if there happens to be another allocation already there, so don't DRET
  if (status != ZX_OK)
    return false;
  DASSERT(child_addr == addr);

  uintptr_t ptr;
  status = child_vmar.map(0, vmo_, offset, length,
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, &ptr);
  if (status != ZX_OK)
    return DRETF(false, "failed to map vmo");
  DASSERT(ptr == addr);
  virt_addr_ = reinterpret_cast<void*>(ptr);
  vmar_ = std::move(child_vmar);

  map_count_++;

  DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);
  return true;
}

bool ZirconPlatformBuffer::MapCpu(void** addr_out, uint64_t alignment) {
  if (!magma::is_page_aligned(alignment))
    return DRETF(false, "alignment 0x%lx isn't page aligned", alignment);
  if (alignment && !magma::is_pow2(alignment))
    return DRETF(false, "alignment 0x%lx isn't power of 2", alignment);
  if (map_count_ == 0) {
    DASSERT(!virt_addr_);
    DASSERT(!vmar_);
    uintptr_t ptr;
    uintptr_t child_addr;
    // If alignment is needed, allocate a vmar that's large enough so that
    // the buffer will fit at an aligned address inside it.
    uintptr_t vmar_size = alignment ? size() + alignment : size();
    zx::vmar child_vmar;
    zx_status_t status = parent_vmar_->get()->allocate(
        0, vmar_size, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC,
        &child_vmar, &child_addr);
    if (status != ZX_OK)
      return DRETF(false, "failed to make vmar");
    uintptr_t offset = alignment ? magma::round_up(child_addr, alignment) - child_addr : 0;
    status = child_vmar.map(offset, vmo_, 0, size(),
                            ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, &ptr);
    if (status != ZX_OK)
      return DRETF(false, "failed to map vmo");

    virt_addr_ = reinterpret_cast<void*>(ptr);
    vmar_ = std::move(child_vmar);
  }

  DASSERT(!alignment || (reinterpret_cast<uintptr_t>(virt_addr_) & (alignment - 1)) == 0);

  *addr_out = virt_addr_;
  map_count_++;

  DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);

  return true;
}

bool ZirconPlatformBuffer::UnmapCpu() {
  DLOG("UnmapCpu vmo %p, map_count_ %u", this, map_count_);
  if (map_count_) {
    map_count_--;
    if (map_count_ == 0) {
      DLOG("map_count 0 unmapping vmo %p", this);
      zx_status_t status = vmar_unmap();
      if (status != ZX_OK)
        DRETF(false, "failed to unmap vmo: %d", status);
    }
    return true;
  }
  return DRETF(false, "attempting to unmap buffer that isnt mapped");
}

bool ZirconPlatformBuffer::CommitPages(uint32_t start_page_index, uint32_t page_count) const {
  TRACE_DURATION("magma", "CommitPages");
  if (!page_count)
    return true;

  if ((start_page_index + page_count) * PAGE_SIZE > size())
    return DRETF(false, "offset + length greater than buffer size");

  zx_status_t status = vmo_.op_range(ZX_VMO_OP_COMMIT, start_page_index * PAGE_SIZE,
                                     page_count * PAGE_SIZE, nullptr, 0);

  if (status == ZX_ERR_NO_MEMORY)
    return DRETF(false,
                 "Kernel returned ZX_ERR_NO_MEMORY when attempting to commit %u vmo "
                 "pages (%u bytes).\nThis means the system has run out of physical memory and "
                 "things will now start going very badly.\nPlease stop using so much "
                 "physical memory or download more RAM at www.downloadmoreram.com :)",
                 page_count, PAGE_SIZE * page_count);
  else if (status != ZX_OK)
    return DRETF(false, "failed to commit vmo pages: %d", status);

  return true;
}

bool ZirconPlatformBuffer::CleanCache(uint64_t offset, uint64_t length, bool invalidate) {
#if defined(__aarch64__)
  if (map_count_) {
    uint32_t op = ZX_CACHE_FLUSH_DATA;
    if (invalidate)
      op |= ZX_CACHE_FLUSH_INVALIDATE;
    if (offset + length > size())
      return DRETF(false, "size too large for buffer");
    zx_status_t status = zx_cache_flush(static_cast<uint8_t*>(virt_addr_) + offset, length, op);
    if (status != ZX_OK)
      return DRETF(false, "failed to clean cache: %d", status);
    return true;
  }
#endif

  uint32_t op = invalidate ? ZX_VMO_OP_CACHE_CLEAN_INVALIDATE : ZX_VMO_OP_CACHE_CLEAN;
  zx_status_t status = vmo_.op_range(op, offset, length, nullptr, 0);
  if (status != ZX_OK)
    return DRETF(false, "failed to clean cache: %d", status);
  return true;
}

bool ZirconPlatformBuffer::SetCachePolicy(magma_cache_policy_t cache_policy) {
  uint32_t zx_cache_policy;
  switch (cache_policy) {
    case MAGMA_CACHE_POLICY_CACHED:
      zx_cache_policy = ZX_CACHE_POLICY_CACHED;
      break;

    case MAGMA_CACHE_POLICY_WRITE_COMBINING:
      zx_cache_policy = ZX_CACHE_POLICY_WRITE_COMBINING;
      break;

    case MAGMA_CACHE_POLICY_UNCACHED:
      zx_cache_policy = ZX_CACHE_POLICY_UNCACHED;
      break;

    default:
      return DRETF(false, "Invalid cache policy %d", cache_policy);
  }

  zx_status_t status = zx_vmo_set_cache_policy(vmo_.get(), zx_cache_policy);
  return DRETF(status == ZX_OK, "zx_vmo_set_cache_policy failed with status %d", status);
}

magma_status_t ZirconPlatformBuffer::GetCachePolicy(magma_cache_policy_t* cache_policy_out) {
  zx_info_vmo_t vmo_info;
  zx_status_t status = vmo_.get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, 0);
  if (status != ZX_OK) {
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "ZX_INFO_VMO returned status: %d", status);
  }
  switch (vmo_info.cache_policy) {
    case ZX_CACHE_POLICY_CACHED:
      *cache_policy_out = MAGMA_CACHE_POLICY_CACHED;
      return MAGMA_STATUS_OK;

    case ZX_CACHE_POLICY_UNCACHED:
      *cache_policy_out = MAGMA_CACHE_POLICY_UNCACHED;
      return MAGMA_STATUS_OK;

    case ZX_CACHE_POLICY_WRITE_COMBINING:
      *cache_policy_out = MAGMA_CACHE_POLICY_WRITE_COMBINING;
      return MAGMA_STATUS_OK;

    default:
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Unknown cache policy: %d",
                      vmo_info.cache_policy);
  }
}

magma_status_t ZirconPlatformBuffer::GetIsMappable(magma_bool_t* is_mappable_out) {
  zx_info_handle_basic handle_info;
  zx_status_t status =
      vmo_.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK)
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to get mappability: %d", status);

  constexpr uint32_t kNeededFlags = ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE;
  *is_mappable_out = (handle_info.rights & kNeededFlags) == kNeededFlags;
  return MAGMA_STATUS_OK;
}

magma::Status ZirconPlatformBuffer::SetMappingAddressRange(
    std::unique_ptr<PlatformBuffer::MappingAddressRange> address_range) {
  DASSERT(address_range);

  auto zircon_address_range = std::static_pointer_cast<MappingAddressRange>(
      std::shared_ptr<PlatformBuffer::MappingAddressRange>(std::move(address_range)));

  if (zircon_address_range->is_root() && parent_vmar_->is_root())
    return MAGMA_STATUS_OK;

  if (map_count_)
    return DRET_MSG(MAGMA_STATUS_ACCESS_DENIED, "Can't set mapping address range while mapped");

  parent_vmar_ = std::move(zircon_address_range);
  return MAGMA_STATUS_OK;
}

bool ZirconPlatformBuffer::Read(void* buffer, uint64_t offset, uint64_t length) {
  zx_status_t status = vmo_.read(buffer, offset, length);
  return DRETF(status == ZX_OK, "Read failed with status: %d", status);
}

bool ZirconPlatformBuffer::Write(const void* buffer, uint64_t offset, uint64_t length) {
  zx_status_t status = vmo_.write(buffer, offset, length);
  return DRETF(status == ZX_OK, "Write failed with status: %d", status);
}

uint64_t PlatformBuffer::MinimumMappableAddress() {
  return MappingAddressRange::CreateDefault()->Base();
}

uint64_t PlatformBuffer::MappableAddressRegionLength() {
  return PlatformBuffer::MappingAddressRange::CreateDefault()->Length();
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size, const char* name) {
  size = magma::round_up(size, PAGE_SIZE);
  if (size == 0)
    return DRETP(nullptr, "attempting to allocate 0 sized buffer");

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK)
    return DRETP(nullptr, "failed to allocate vmo size %" PRId64 ": %d", size, status);
  vmo.set_property(ZX_PROP_NAME, name, strlen(name));

  DLOG("allocated vmo size %ld handle 0x%x", size, vmo.get());
  return std::make_unique<ZirconPlatformBuffer>(std::move(vmo), size);
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle) {
  uint64_t size;
  // presumably this will fail if handle is invalid or not a vmo handle, so we perform no
  // additional error checking
  zx::vmo vmo(handle);
  auto status = vmo.get_size(&size);

  if (status != ZX_OK)
    return DRETP(nullptr, "zx_vmo_get_size failed");

  if (!magma::is_page_aligned(size))
    return DRETP(nullptr, "attempting to import vmo with invalid size");

  return std::make_unique<ZirconPlatformBuffer>(std::move(vmo), size);
}

}  // namespace magma
