// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/memory.h"

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session.h"

namespace {

bool IsHostMemoryGpuMappable() {
  // TODO(MZ-998): Decide how to determine if we're on an UMA platform
  // or not.
  return true;
}

}  // namespace

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Memory::kTypeInfo = {ResourceType::kMemory, "Memory"};

Memory::Memory(Session* session, ResourceId id,
               ::fuchsia::ui::gfx::MemoryArgs args)
    : Resource(session, id, kTypeInfo),
      is_host_(args.memory_type == fuchsia::images::MemoryType::HOST_MEMORY),
      shared_vmo_(fxl::MakeRefCounted<fsl::SharedVmo>(std::move(args.vmo),
                                                      ZX_VM_PERM_READ)),
      allocation_size_(args.allocation_size) {
  FXL_DCHECK(args.allocation_size > 0);
}

MemoryPtr Memory::New(Session* session, ResourceId id,
                      ::fuchsia::ui::gfx::MemoryArgs args,
                      ErrorReporter* error_reporter) {
  if (args.allocation_size == 0) {
    error_reporter->ERROR() << "Memory::New(): allocation_size argument ("
                            << args.allocation_size << ") is not valid.";
    return nullptr;
  }

  uint64_t size;
  auto status = args.vmo.get_size(&size);

  if (status != ZX_OK) {
    error_reporter->ERROR()
        << "Memory::New(): zx_vmo_get_size failed (err=" << status << ").";
    return nullptr;
  }

  if (args.allocation_size > size) {
    error_reporter->ERROR()
        << "Memory::New(): allocation_size (" << args.allocation_size
        << ") is larger than the size of the corresponding vmo (" << size
        << ").";
    return nullptr;
  }
  return fxl::AdoptRef(new Memory(session, id, std::move(args)));
}

escher::GpuMemPtr Memory::ImportGpuMemory() {
  TRACE_DURATION("gfx", "Memory::ImportGpuMemory");
  // TODO(MZ-151): If we're allowed to import the same vmo twice to two
  // different resources, we may need to change driver semantics so that you can
  // import a VMO twice. Referencing the test bug for now, since it should
  // uncover the bug.
  if (is_host_ && !IsHostMemoryGpuMappable()) {
    return nullptr;
  }

  vk::DeviceMemory memory = nullptr;

  // Import a VkDeviceMemory from the VMO. VkAllocateMemory takes ownership of
  // the VMO handle it is passed.
  vk::ImportMemoryZirconHandleInfoFUCHSIA memory_import_info(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA,
      DuplicateVmo().release());

  vk::MemoryAllocateInfo memory_allocate_info(
      size(), session()->resource_context().imported_memory_type_index);
  memory_allocate_info.setPNext(&memory_import_info);

  vk::Result err = session()->resource_context().vk_device.allocateMemory(
      &memory_allocate_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    error_reporter()->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                                 "VkAllocateMemory failed.";
    return nullptr;
  }

  // TODO(SCN-1115): If we can rely on all memory being importable into Vulkan
  // (either as host or device memory), then we can always make a GpuMem object,
  // and rely on its mapped pointer accessor instead of storing our own local
  // uint8_t*.
  return escher::GpuMem::AdoptVkMemory(
      session()->resource_context().vk_device, vk::DeviceMemory(memory),
      vk::DeviceSize(size()), is_host_ /* needs_mapped_ptr */);
}

zx::vmo Memory::DuplicateVmo() {
  zx::vmo the_clone;
  zx_status_t status =
      shared_vmo_->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &the_clone);
  ZX_ASSERT_MSG(status == ZX_OK, "duplicate failed: status=%d", status);
  return the_clone;
}

}  // namespace gfx
}  // namespace scenic_impl
