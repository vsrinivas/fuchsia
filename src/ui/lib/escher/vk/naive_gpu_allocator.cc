// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/naive_gpu_allocator.h"

#include "src/ui/lib/escher/impl/gpu_mem_slab.h"
#include "src/ui/lib/escher/impl/naive_buffer.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

NaiveGpuAllocator::NaiveGpuAllocator(const VulkanContext& context)
    : physical_device_(context.physical_device), device_(context.device) {
  FX_DCHECK(device_);
}

NaiveGpuAllocator::~NaiveGpuAllocator() {
  FX_CHECK(total_slab_bytes_ == 0);
  FX_CHECK(slab_count_ == 0);
}

GpuMemPtr NaiveGpuAllocator::AllocateMemory(vk::MemoryRequirements reqs,
                                            vk::MemoryPropertyFlags flags) {
  TRACE_DURATION("gfx", "escher::NaiveGpuAllocator::AllocateMemory");

  // TODO (fxbug.dev/23951): need to manually overallocate and adjust offset to ensure
  // alignment, based on the content of reqs.alignment?  Probably not, but
  // should verify.
  vk::DeviceMemory vk_mem;
  uint32_t memory_type_index = 0;
  bool needs_mapped_ptr = false;
  // Determine whether we will need to map the memory.
  if (flags & vk::MemoryPropertyFlagBits::eHostVisible) {
    // We don't currently provide an interface for flushing mapped data, so
    // ensure that the allocated memory is cache-coherent.  This is more
    // convenient anyway.
    flags |= vk::MemoryPropertyFlagBits::eHostCoherent;
    needs_mapped_ptr = true;
  }

  // TODO (fxbug.dev/24367): cache flags for efficiency? Or perhaps change signature of
  // this method to directly take the memory-type index.
  memory_type_index = impl::GetMemoryTypeIndex(physical_device_, reqs.memoryTypeBits, flags);

  {
    TRACE_DURATION("gfx", "vk::Device::allocateMemory");
    vk::MemoryAllocateInfo info;
    info.allocationSize = reqs.size;
    info.memoryTypeIndex = memory_type_index;
    vk_mem = ESCHER_CHECKED_VK_RESULT(device_.allocateMemory(info));
  }

  return fxl::AdoptRef(new impl::GpuMemSlab(device_, vk_mem, reqs.size, needs_mapped_ptr, this));
}

BufferPtr NaiveGpuAllocator::AllocateBuffer(ResourceManager* manager, vk::DeviceSize size,
                                            vk::BufferUsageFlags usage_flags,
                                            vk::MemoryPropertyFlags memory_property_flags,
                                            GpuMemPtr* out_ptr) {
  TRACE_DURATION("gfx", "escher::NaiveGpuAllocator::AllocateBuffer");
  // NaiveBuffer requires a real manager pointer to properly function.
  FX_DCHECK(manager);

  // Create buffer.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = size;
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer = ESCHER_CHECKED_VK_RESULT(device_.createBuffer(buffer_create_info));

  auto memory_requirements = device_.getBufferMemoryRequirements(vk_buffer);

  // Allocate memory for the buffer.
  GpuMemPtr mem = AllocateMemory(memory_requirements, memory_property_flags);
  FX_DCHECK(mem->size() >= size)
      << "Size of allocated memory should not be less than requested size";

  if (out_ptr) {
    *out_ptr = mem;
  }
  return impl::NaiveBuffer::AdoptVkBuffer(manager, std::move(mem), size, vk_buffer);
}

ImagePtr NaiveGpuAllocator::AllocateImage(ResourceManager* manager, const ImageInfo& info,
                                          GpuMemPtr* out_ptr) {
  constexpr auto kInitialLayout = vk::ImageLayout::eUndefined;

  // Check if the image create info above is valid.
  if (!impl::CheckImageCreateInfoValidity(
          physical_device_, image_utils::CreateVkImageCreateInfo(info, kInitialLayout))) {
    FX_LOGS(ERROR) << "NaiveGpuAllocator::AllocateImage(): ImageCreateInfo invalid. Create failed.";
    return ImagePtr();
  }

  vk::Image image = image_utils::CreateVkImage(device_, info, kInitialLayout);

  // Allocate memory and bind it to the image.
  vk::MemoryRequirements reqs = device_.getImageMemoryRequirements(image);
  escher::GpuMemPtr mem = AllocateMemory(reqs, info.memory_flags);

  if (out_ptr) {
    *out_ptr = mem;
  }
  ImagePtr escher_image =
      impl::NaiveImage::AdoptVkImage(manager, info, image, std::move(mem), kInitialLayout);
  FX_CHECK(escher_image);
  return escher_image;
}

size_t NaiveGpuAllocator::GetTotalBytesAllocated() const { return total_slab_bytes_; }

void NaiveGpuAllocator::OnSlabCreated(vk::DeviceSize slab_size) {
  ++slab_count_;
  total_slab_bytes_ += slab_size;
}

void NaiveGpuAllocator::OnSlabDestroyed(vk::DeviceSize slab_size) {
  --slab_count_;
  total_slab_bytes_ -= slab_size;
}

}  // namespace escher
