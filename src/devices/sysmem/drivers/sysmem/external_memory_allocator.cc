// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "external_memory_allocator.h"

#include <fbl/string_printf.h>

#include "macros.h"

namespace sysmem_driver {
ExternalMemoryAllocator::ExternalMemoryAllocator(MemoryAllocator::Owner* owner,
                                                 fidl::Client<llcpp::fuchsia::sysmem2::Heap> heap,
                                                 std::unique_ptr<async::Wait> wait_for_close,
                                                 llcpp::fuchsia::sysmem2::HeapProperties properties)
    : MemoryAllocator(std::move(properties)),
      owner_(owner),
      heap_(std::move(heap)),
      wait_for_close_(std::move(wait_for_close)) {
  node_ = owner->heap_node()->CreateChild(
      fbl::StringPrintf("ExternalMemoryAllocator-%ld", id()).c_str());
  node_.CreateUint("id", id(), &properties_);
}

ExternalMemoryAllocator::~ExternalMemoryAllocator() { ZX_DEBUG_ASSERT(is_empty()); }

zx_status_t ExternalMemoryAllocator::Allocate(uint64_t size, std::optional<std::string> name,
                                              zx::vmo* parent_vmo) {
  auto result = heap_->AllocateVmo_Sync(size);
  if (!result.ok() || result.value().s != ZX_OK) {
    DRIVER_ERROR("HeapAllocate() failed - status: %d status2: %d", result.status(),
                 result.value().s);
    // sanitize to ZX_ERR_NO_MEMORY regardless of why.
    return ZX_ERR_NO_MEMORY;
  }
  zx::vmo result_vmo = std::move(result.value().vmo);
  constexpr const char vmo_name[] = "Sysmem-external-heap";
  result_vmo.set_property(ZX_PROP_NAME, vmo_name, sizeof(vmo_name));
  *parent_vmo = std::move(result_vmo);
  return ZX_OK;
}

zx_status_t ExternalMemoryAllocator::SetupChildVmo(
    const zx::vmo& parent_vmo, const zx::vmo& child_vmo,
    llcpp::fuchsia::sysmem2::SingleBufferSettings buffer_settings) {
  zx::vmo child_vmo_copy;
  zx_status_t status = child_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &child_vmo_copy);
  if (status != ZX_OK) {
    DRIVER_ERROR("duplicate() failed - status: %d", status);
    // sanitize to ZX_ERR_NO_MEMORY regardless of why.
    status = ZX_ERR_NO_MEMORY;
    return status;
  }

  auto result = heap_->CreateResource_Sync(std::move(child_vmo_copy), std::move(buffer_settings));
  if (!result.ok() || result.value().s != ZX_OK) {
    DRIVER_ERROR("HeapCreateResource() failed - status: %d status2: %d", result.status(),
                 result.value().s);
    // sanitize to ZX_ERR_NO_MEMORY regardless of why.
    return ZX_ERR_NO_MEMORY;
  }
  allocations_[parent_vmo.get()] = result.value().id;
  return ZX_OK;
}

void ExternalMemoryAllocator::Delete(zx::vmo parent_vmo) {
  auto it = allocations_.find(parent_vmo.get());
  if (it == allocations_.end()) {
    DRIVER_ERROR("Invalid allocation - vmo_handle: %d", parent_vmo.get());
    return;
  }
  auto id = it->second;
  auto result = heap_->DestroyResource_Sync(id);
  if (!result.ok()) {
    DRIVER_ERROR("HeapDestroyResource() failed - status: %d", result.status());
    // fall-through - this can only fail because resource has
    // already been destroyed.
  }
  allocations_.erase(it);
  if (is_empty()) {
    owner_->CheckForUnbind();
  }
  // ~parent_vmo
}

}  // namespace sysmem_driver
