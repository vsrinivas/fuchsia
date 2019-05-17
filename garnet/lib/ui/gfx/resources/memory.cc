// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/memory.h"

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/session.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace {

// TODO(SCN-1368): This is a hack until we solve the memory importation bug. On
// x86 platforms, vk::Buffers come out of a separate memory pool. These helper
// functions help make sure that there is a single valid memory pool, for
// both images and buffers, by creating a dummy representative buffer/image.
uint32_t GetBufferMemoryBits(vk::Device device) {
  static vk::Device cached_device;
  static uint32_t cached_bits;
  if (cached_device == device) {
    return cached_bits;
  }

  constexpr vk::DeviceSize kUnimportantBufferSize = 30000;
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = kUnimportantBufferSize;
  // TODO(SCN-1369): Buffer creation parameters currently need to be the same
  // across all Scenic import flows, as well as in client export objects.
  buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc |
                             vk::BufferUsageFlagBits::eTransferDst |
                             vk::BufferUsageFlagBits::eStorageTexelBuffer |
                             vk::BufferUsageFlagBits::eStorageBuffer |
                             vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer =
      escher::ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

  vk::MemoryRequirements reqs = device.getBufferMemoryRequirements(vk_buffer);
  device.destroyBuffer(vk_buffer);
  cached_device = device;
  cached_bits = reqs.memoryTypeBits;
  return cached_bits;
}

uint32_t GetImageMemoryBits(vk::Device device) {
  static vk::Device cached_device;
  static uint32_t cached_bits;
  if (cached_device == device) {
    return cached_bits;
  }

  constexpr uint32_t kUnimportantImageSize = 1024;
  escher::ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.width = kUnimportantImageSize;
  info.height = kUnimportantImageSize;
  // The image creation parameters need to be the same as those in scenic
  // (garnet/lib/ui/gfx/resources/gpu_image.cc and
  // src/ui/lib/escher/util/image_utils.cc) or else the different vulkan
  // devices may interpret the bytes differently.
  // TODO(SCN-1369): Use API to coordinate this with scenic.
  info.usage = vk::ImageUsageFlagBits::eTransferSrc |
               vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eColorAttachment;
  vk::Image image = escher::image_utils::CreateVkImage(device, info);
  vk::MemoryRequirements reqs = device.getImageMemoryRequirements(image);
  device.destroyImage(image);
  cached_device = device;
  cached_bits = reqs.memoryTypeBits;
  return cached_bits;
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

  auto retval = fxl::AdoptRef(new Memory(session, id, std::move(args)));

  if (!retval->is_host()) {
    if (!retval->GetGpuMem()) {
      // Device memory must be able to be imported to the GPU. If not, this
      // command is an error and the client should be notified. GetGpuMem() will
      // provide a valid error message, but this factory must fail in order to
      // signal to the command applier that the channel should be closed.
      return nullptr;
    }
  }

  return retval;
}

escher::GpuMemPtr Memory::ImportGpuMemory() {
  TRACE_DURATION("gfx", "Memory::ImportGpuMemory");

  auto vk_device = session()->resource_context().vk_device;

  // TODO(SCN-151): If we're allowed to import the same vmo twice to two
  // different resources, we may need to change driver semantics so that you
  // can import a VMO twice. Referencing the test bug for now, since it should
  // uncover the bug.
  vk::MemoryZirconHandlePropertiesFUCHSIA handle_properties;
  vk::Result err = vk_device.getMemoryZirconHandlePropertiesFUCHSIA(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA,
      shared_vmo_->vmo().get(), &handle_properties,
      session()->resource_context().vk_loader);
  if (err != vk::Result::eSuccess) {
    error_reporter()->ERROR()
        << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
           "VkGetMemoryFuchsiaHandlePropertiesKHR failed.";
    return nullptr;
  }
  if (handle_properties.memoryTypeBits == 0) {
    if (!is_host_) {
      error_reporter()->ERROR()
          << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
             "VkGetMemoryFuchsiaHandlePropertiesKHR "
             "returned zero valid memory types.";
    } else {
      // Importing read-only host memory into the Vulkan driver should not work,
      // but it is not an error to try to do so. Returning a nullptr here should
      // not result in a closed session channel, as this flow should only happen
      // when Scenic is attempting to optimize image importation. See SCN-1012
      // for other issues this this flow.
      FXL_LOG(INFO) << "Host memory VMO could not be imported to any valid "
                       "Vulkan memory types.";
    }
    return nullptr;
  }

  // TODO(SCN-1012): This function is only used on host memory when we are
  // performing a zero-copy import. So it is currently hardcoded to look for a
  // valid UMA-style memory pool -- one that can be used as both host and device
  // memory.
  vk::MemoryPropertyFlags required_flags =
      is_host_ ? vk::MemoryPropertyFlagBits::eDeviceLocal |
                     vk::MemoryPropertyFlagBits::eHostVisible
               : vk::MemoryPropertyFlagBits::eDeviceLocal;

  auto vk_physical_device = session()->resource_context().vk_physical_device;

  uint32_t memory_type_bits = handle_properties.memoryTypeBits;

// TODO(SCN-1368): This code should be unnecessary once we have a code flow that
// understands how the memory is expected to be used.
#if __x86_64__
  memory_type_bits &= GetBufferMemoryBits(vk_device);
  memory_type_bits &= GetImageMemoryBits(vk_device);
  FXL_CHECK(memory_type_bits != 0)
      << "This platform does not have a single memory pool that is valid for "
         "both images and buffers. Please fix SCN-1368.";
#endif  // __x86_64__

  uint32_t memory_type_index = escher::impl::GetMemoryTypeIndex(
      vk_physical_device, memory_type_bits, required_flags);

  vk::PhysicalDeviceMemoryProperties memory_types =
      vk_physical_device.getMemoryProperties();
  if (memory_type_index >= memory_types.memoryTypeCount) {
    if (!is_host_) {
      // Because vkGetMemoryZirconHandlePropertiesFUCHSIA may work on normal CPU
      // memory on UMA platforms, importation failure is only an error for
      // device memory.
      error_reporter()->ERROR()
          << "scenic_impl::gfx::Memory::ImportGpuMemory(): could not find a "
             "valid memory type for importation.";
    } else {
      // TODO(SCN-1012): Error message is UMA specific.
      FXL_LOG(INFO)
          << "Host memory VMO could not find a UMA-style memory type.";
    }
    return nullptr;
  }

  // Import a VkDeviceMemory from the VMO. VkAllocateMemory takes ownership of
  // the VMO handle it is passed.
  vk::ImportMemoryZirconHandleInfoFUCHSIA memory_import_info(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA,
      DuplicateVmo().release());
  vk::MemoryAllocateInfo memory_allocate_info(size(), memory_type_index);
  memory_allocate_info.setPNext(&memory_import_info);

  vk::DeviceMemory memory = nullptr;
  err = vk_device.allocateMemory(&memory_allocate_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    error_reporter()->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                                 "VkAllocateMemory failed.";
    return nullptr;
  }

  // TODO(SCN-1115): If we can rely on all memory being importable into Vulkan
  // (either as host or device memory), then we can always make a GpuMem
  // object, and rely on its mapped pointer accessor instead of storing our
  // own local uint8_t*.
  return escher::GpuMem::AdoptVkMemory(vk_device, vk::DeviceMemory(memory),
                                       vk::DeviceSize(size()),
                                       is_host_ /* needs_mapped_ptr */);
}

zx::vmo Memory::DuplicateVmo() {
  zx::vmo the_clone;
  zx_status_t status =
      shared_vmo_->vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &the_clone);
  ZX_ASSERT_MSG(status == ZX_OK, "duplicate failed: status=%d", status);
  return the_clone;
}

uint32_t Memory::HasSharedMemoryPools(vk::Device device,
                                      vk::PhysicalDevice physical_device) {
  vk::MemoryPropertyFlags required_flags =
      vk::MemoryPropertyFlagBits::eDeviceLocal |
      vk::MemoryPropertyFlagBits::eHostVisible;

  uint32_t memory_type_bits =
      GetBufferMemoryBits(device) & GetImageMemoryBits(device);

  uint32_t memory_type_index = escher::impl::GetMemoryTypeIndex(
      physical_device, memory_type_bits, required_flags);

  vk::PhysicalDeviceMemoryProperties memory_types =
      physical_device.getMemoryProperties();
  return memory_type_index < memory_types.memoryTypeCount;
}

}  // namespace gfx
}  // namespace scenic_impl
