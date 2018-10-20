// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/memory.h"

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session.h"

namespace {

bool IsGpuMappable() {
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
      allocation_size_(args.allocation_size) {}

MemoryPtr Memory::New(Session* session, ResourceId id,
                      ::fuchsia::ui::gfx::MemoryArgs args) {
  return fxl::AdoptRef(new Memory(session, id, std::move(args)));
}

escher::GpuMemPtr Memory::ImportGpuMemory() {
  TRACE_DURATION("gfx", "Memory::ImportGpuMemory");
  // TODO(MZ-151): If we're allowed to import the same vmo twice to two
  // different resources, we may need to change driver semantics so that you can
  // import a VMO twice. Referencing the test bug for now, since it should
  // uncover the bug.
  if (!IsGpuMappable()) {
    return nullptr;
  }

  vk::DeviceMemory memory = nullptr;

  // Import a VkDeviceMemory from the VMO. VkAllocateMemory takes ownership of
  // the VMO handle it is passed.
  vk::ImportMemoryFuchsiaHandleInfoKHR memory_import_info(
      vk::ExternalMemoryHandleTypeFlagBits::eFuchsiaVmoKHR,
      DuplicateVmo().release());

  vk::MemoryAllocateInfo memory_allocate_info(
      size(), session()->engine()->imported_memory_type_index());
  memory_allocate_info.setPNext(&memory_import_info);

  vk::Result err = session()->engine()->vk_device().allocateMemory(
      &memory_allocate_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    error_reporter()->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                                 "VkAllocateMemory failed.";
    return nullptr;
  }

  // TODO(MZ-388): Need to be able to get the memory type index using
  // vkGetMemoryFuchsiaHandlePropertiesKHR.
  uint32_t memory_type_index = 0;

  return escher::GpuMem::New(session()->engine()->vk_device(),
                             vk::DeviceMemory(memory), vk::DeviceSize(size()),
                             memory_type_index);
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
