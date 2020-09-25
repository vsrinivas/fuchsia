// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/memory.h"

#include <lib/trace/event.h>
#include <zircon/status.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"

namespace {

// TODO(fxbug.dev/24562): This is a hack until we solve the memory importation bug. On
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
  // TODO(fxbug.dev/24563): Buffer creation parameters currently need to be the same
  // across all Scenic import flows, as well as in client export objects.
  buffer_create_info.usage =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
      vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer = escher::ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

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
  // (src/ui/scenic/lib/gfx/resources/gpu_image.cc and
  // src/ui/lib/escher/util/image_utils.cc) or else the different vulkan
  // devices may interpret the bytes differently.
  // TODO(fxbug.dev/24563): Use API to coordinate this with scenic.
  info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
  vk::Image image = escher::image_utils::CreateVkImage(device, info, vk::ImageLayout::eUndefined);
  vk::MemoryRequirements reqs = device.getImageMemoryRequirements(image);
  device.destroyImage(image);
  cached_device = device;
  cached_bits = reqs.memoryTypeBits;
  return cached_bits;
}

// Initialize |alloc_info| and |memory_import_info| with the given parameters.
//
// Returns true if it succeeds setting the |alloc_info| and
// |memory_import_info|, otherwise it returns false and outputs error message
// to |reporter|.
bool InitializeMemoryAllocateInfo(const scenic_impl::gfx::ResourceContext& resource_context,
                                  const zx::vmo* vmo, bool is_host, uint64_t size,
                                  scenic_impl::ErrorReporter* reporter,
                                  vk::MemoryAllocateInfo* alloc_info,
                                  vk::ImportMemoryZirconHandleInfoFUCHSIA* memory_import_info) {
  FX_DCHECK(alloc_info);
  FX_DCHECK(memory_import_info);

  // We first check the rights of vmo to ensure that it has read, write and
  // duplicate rights.
  zx_info_handle_basic_t vmo_info;
  auto get_info_status =
      vmo->get_info(ZX_INFO_HANDLE_BASIC, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
  if (get_info_status != ZX_OK) {
    reporter->ERROR()
        << "scenic_impl::gfx::Memory::ImportGpuMemory(): Cannot get VMO info, status: "
        << zx_status_get_string(get_info_status);
    return false;
  }

  // Currently Magma doesn't support import of read-only VMOs. In order to make
  // the behavior of ImportGpuMemory() consistent among different Vulkan ICDs,
  // we enforce that the imported vmo should have both read and write rights for
  // all device memory.
  // Therefore, we require all VMOs to have read and write rights.
  if (!is_host && !(vmo_info.rights & ZX_RIGHT_READ)) {
    reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): VMO doesn't have "
                         "right ZX_RIGHT_READ";
    return false;
  }
  if (!is_host && !(vmo_info.rights & ZX_RIGHT_WRITE)) {
    reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): VMO doesn't have "
                         "right ZX_RIGHT_WRITE";
    return false;
  }

  auto vk_device = resource_context.vk_device;
  // TODO(fxbug.dev/23406): If we're allowed to import the same vmo twice to two
  // different resources, we may need to change driver semantics so that you
  // can import a VMO twice. Referencing the test bug for now, since it should
  // uncover the bug.
  vk::MemoryZirconHandlePropertiesFUCHSIA handle_properties;
  vk::Result err = vk_device.getMemoryZirconHandlePropertiesFUCHSIA(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA, vmo->get(), &handle_properties,
      resource_context.vk_loader);
  if (err != vk::Result::eSuccess) {
    reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                         "VkGetMemoryFuchsiaHandlePropertiesKHR failed.";
    return false;
  }
  if (handle_properties.memoryTypeBits == 0) {
    if (!is_host) {
      reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                           "VkGetMemoryFuchsiaHandlePropertiesKHR "
                           "returned zero valid memory types.";
    } else {
      // Importing read-only host memory into the Vulkan driver should not work,
      // but it is not an error to try to do so. Returning a nullptr here should
      // not result in a closed session channel, as this flow should only happen
      // when Scenic is attempting to optimize image importation. See fxbug.dev/24225
      // for other issues this this flow.
      FX_LOGS(INFO) << "Host memory VMO could not be imported to any valid Vulkan memory types.";
    }
    return false;
  }

  // TODO(fxbug.dev/24225): This function is only used on host memory when we are
  // performing a zero-copy import. So it is currently hardcoded to look for a
  // valid UMA-style memory pool -- one that can be used as both host and device
  // memory.
  vk::MemoryPropertyFlags required_flags =
      is_host ? vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible
              : vk::MemoryPropertyFlagBits::eDeviceLocal;

  auto vk_physical_device = resource_context.vk_physical_device;

  uint32_t memory_type_bits = handle_properties.memoryTypeBits;

// TODO(fxbug.dev/24562): This code should be unnecessary once we have a code flow that
// understands how the memory is expected to be used.
#if __x86_64__
  memory_type_bits &= GetBufferMemoryBits(vk_device);
  memory_type_bits &= GetImageMemoryBits(vk_device);
  FX_CHECK(memory_type_bits != 0)
      << "This platform does not have a single memory pool that is valid for "
         "both images and buffers. Please fix fxbug.dev/24562.";
#endif  // __x86_64__

  uint32_t memory_type_index =
      escher::impl::GetMemoryTypeIndex(vk_physical_device, memory_type_bits, required_flags);

  vk::PhysicalDeviceMemoryProperties memory_types = vk_physical_device.getMemoryProperties();
  if (memory_type_index >= memory_types.memoryTypeCount) {
    if (!is_host) {
      // Because vkGetMemoryZirconHandlePropertiesFUCHSIA may work on normal CPU
      // memory on UMA platforms, importation failure is only an error for
      // device memory.
      reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): could not find a "
                           "valid memory type for importation.";
    } else {
      // TODO(fxbug.dev/24225): Error message is UMA specific.
      FX_LOGS(INFO) << "Host memory VMO could not find a UMA-style memory type.";
    }
    return false;
  }

  // Import a VkDeviceMemory from the VMO. VkAllocateMemory takes ownership of
  // the VMO handle it is passed.
  zx::vmo duplicated_vmo;
  zx_status_t status = vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicated_vmo);
  if (status != ZX_OK) {
    reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): cannot duplicate VMO, "
                         "status: "
                      << zx_status_get_string(status);
    return false;
  }
  *memory_import_info = vk::ImportMemoryZirconHandleInfoFUCHSIA(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA, duplicated_vmo.release());
  *alloc_info = vk::MemoryAllocateInfo(size, memory_type_index);
  alloc_info->setPNext(memory_import_info);
  return true;
}

}  // namespace

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Memory::kTypeInfo = {ResourceType::kMemory, "Memory"};

Memory::Memory(Session* session, ResourceId id, bool is_host, zx::vmo vmo, uint64_t allocation_size)
    : Resource(session, session->id(), id, kTypeInfo),
      is_host_(is_host),
      shared_vmo_(fxl::MakeRefCounted<fsl::SharedVmo>(std::move(vmo), ZX_VM_PERM_READ)),
      allocation_size_(allocation_size) {}

MemoryPtr Memory::New(Session* session, ResourceId id, ::fuchsia::ui::gfx::MemoryArgs args,
                      ErrorReporter* error_reporter) {
  if (args.allocation_size == 0) {
    error_reporter->ERROR() << "Memory::New(): allocation_size argument (" << args.allocation_size
                            << ") is not valid.";
    return nullptr;
  }

  uint64_t size;
  auto status = args.vmo.get_size(&size);

  if (status != ZX_OK) {
    error_reporter->ERROR() << "Memory::New(): zx_vmo_get_size failed (err=" << status << ").";
    return nullptr;
  }

  if (args.allocation_size > size) {
    error_reporter->ERROR() << "Memory::New(): allocation_size (" << args.allocation_size
                            << ") is larger than the size of the corresponding vmo (" << size
                            << ").";
    return nullptr;
  }

  auto retval = fxl::AdoptRef(
      new Memory(session, id, args.memory_type == fuchsia::images::MemoryType::HOST_MEMORY,
                 std::move(args.vmo), args.allocation_size));
  if (!retval->is_host()) {
    if (!retval->GetGpuMem(error_reporter)) {
      // Device memory must be able to be imported to the GPU. If not, this
      // command is an error and the client should be notified. GetGpuMem() will
      // provide a valid error message, but this factory must fail in order to
      // signal to the command applier that the channel should be closed.
      return nullptr;
    }
  }
  return retval;
}

MemoryPtr Memory::New(Session* session, ResourceId id, zx::vmo vmo,
                      vk::MemoryAllocateInfo alloc_info, ErrorReporter* error_reporter) {
  uint64_t size;
  auto status = vmo.get_size(&size);
  if (status != ZX_OK) {
    error_reporter->ERROR() << "Memory::New(): zx_vmo_get_size failed (err=" << status << ").";
    return nullptr;
  }
  alloc_info.allocationSize = size;

  auto retval = fxl::AdoptRef(
      new Memory(session, id, /*is_host=*/false, std::move(vmo), alloc_info.allocationSize));
  if (!retval->GetGpuMem(error_reporter, &alloc_info)) {
    // It is an error if we cannot map GPU memory through this factory function.
    return nullptr;
  }

  return retval;
}

escher::GpuMemPtr Memory::ImportGpuMemory(ErrorReporter* reporter,
                                          vk::MemoryAllocateInfo* alloc_info) {
  TRACE_DURATION("gfx", "Memory::ImportGpuMemory");

  vk::MemoryAllocateInfo vmo_alloc_info;
  vk::ImportMemoryZirconHandleInfoFUCHSIA memory_import_info;
  if (!alloc_info) {
    const bool retval = InitializeMemoryAllocateInfo(resource_context(), &shared_vmo_->vmo(),
                                                     is_host(), allocation_size_, reporter,
                                                     &vmo_alloc_info, &memory_import_info);
    if (!retval) {
      return nullptr;
    }
    alloc_info = &vmo_alloc_info;
  }

  auto vk_device = resource_context().vk_device;
  vk::DeviceMemory memory = nullptr;
  vk::Result err = vk_device.allocateMemory(alloc_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    reporter->ERROR() << "scenic_impl::gfx::Memory::ImportGpuMemory(): "
                         "VkAllocateMemory failed.";
    return nullptr;
  }
  // TODO(fxbug.dev/24322): If we can rely on all memory being importable into Vulkan
  // (either as host or device memory), then we can always make a GpuMem
  // object, and rely on its mapped pointer accessor instead of storing our
  // own local uint8_t*.
  return escher::GpuMem::AdoptVkMemory(vk_device, vk::DeviceMemory(memory), vk::DeviceSize(size()),
                                       is_host() /* needs_mapped_ptr */);
}

uint32_t Memory::HasSharedMemoryPools(vk::Device device, vk::PhysicalDevice physical_device) {
  vk::MemoryPropertyFlags required_flags =
      vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible;

  uint32_t memory_type_bits = GetBufferMemoryBits(device) & GetImageMemoryBits(device);

  uint32_t memory_type_index =
      escher::impl::GetMemoryTypeIndex(physical_device, memory_type_bits, required_flags);

  vk::PhysicalDeviceMemoryProperties memory_types = physical_device.getMemoryProperties();
  return memory_type_index < memory_types.memoryTypeCount;
}

}  // namespace gfx
}  // namespace scenic_impl
