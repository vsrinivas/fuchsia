// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/vma_gpu_allocator.h"

#include "src/ui/lib/escher/util/image_utils.h"

namespace {

class VmaGpuMem : public escher::GpuMem {
 public:
  VmaGpuMem(VmaAllocator allocator, VmaAllocation allocation,
            VmaAllocationInfo info)
      : GpuMem(info.deviceMemory, info.size, info.offset,
               static_cast<uint8_t*>(info.pMappedData)),
        allocator_(allocator),
        allocation_(allocation) {}

  ~VmaGpuMem() { vmaFreeMemory(allocator_, allocation_); }

 private:
  VmaAllocator allocator_;
  VmaAllocation allocation_;
};

class VmaBuffer : public escher::Buffer {
 public:
  VmaBuffer(escher::ResourceManager* manager, VmaAllocator allocator,
            VmaAllocation allocation, VmaAllocationInfo info, vk::Buffer buffer)
      : Buffer(manager, buffer, info.size,
               static_cast<uint8_t*>(info.pMappedData)),
        allocator_(allocator),
        allocation_(allocation) {}

  ~VmaBuffer() { vmaDestroyBuffer(allocator_, vk(), allocation_); }

 private:
  VmaAllocator allocator_;
  VmaAllocation allocation_;
};

// Vma objects (i.e., buffers, images) with mapped memory are cleaned up by
// destroying the original object, not by destroying a separate memory
// allocation object. However, we can request mapped pointers from vma objects.
// Therefore, we implement an 'out_mem' GpuMem object by keeping a strong
// reference to the original vma object, and wrapping only the mapped pointer,
// offset, and size parameters.
class VmaMappedGpuMem : public escher::GpuMem {
 public:
  VmaMappedGpuMem(VmaAllocationInfo info,
                  const fxl::RefPtr<escher::WaitableResource>& keep_alive)
      : GpuMem(info.deviceMemory, info.size, info.offset,
               static_cast<uint8_t*>(info.pMappedData)),
        keep_alive_(keep_alive) {}

 private:
  const fxl::RefPtr<escher::WaitableResource> keep_alive_;
};

class VmaImage : public escher::Image {
 public:
  VmaImage(escher::ResourceManager* manager, escher::ImageInfo image_info,
           vk::Image image, VmaAllocator allocator, VmaAllocation allocation,
           VmaAllocationInfo allocation_info)
      : Image(manager, image_info, image, allocation_info.size,
              static_cast<uint8_t*>(allocation_info.pMappedData)),
        allocator_(allocator),
        allocation_(allocation) {}

  ~VmaImage() { vmaDestroyImage(allocator_, vk(), allocation_); }

 private:
  VmaAllocator allocator_;
  VmaAllocation allocation_;
};

}  // namespace

namespace escher {

VmaGpuAllocator::VmaGpuAllocator(const VulkanContext& context) {
  FXL_DCHECK(context.device);
  FXL_DCHECK(context.physical_device);
  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.physicalDevice = context.physical_device;
  allocatorInfo.device = context.device;
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
  vmaCreateAllocator(&allocatorInfo, &allocator_);
}

VmaGpuAllocator::~VmaGpuAllocator() { vmaDestroyAllocator(allocator_); }

GpuMemPtr VmaGpuAllocator::AllocateMemory(vk::MemoryRequirements reqs,
                                          vk::MemoryPropertyFlags flags) {
  // Needed so we have a pointer to the C-style type.
  VkMemoryRequirements c_reqs = reqs;

  // VMA specific allocation parameters.
  VmaAllocationCreateInfo create_info = {
      VMA_ALLOCATION_CREATE_MAPPED_BIT,
      VMA_MEMORY_USAGE_UNKNOWN,
      static_cast<VkMemoryPropertyFlags>(flags),
      0u,
      0u,
      VK_NULL_HANDLE,
      nullptr};

  // Output structs.
  VmaAllocation allocation;
  VmaAllocationInfo allocation_info;
  auto status = vmaAllocateMemory(allocator_, &c_reqs, &create_info,
                                  &allocation, &allocation_info);

  FXL_DCHECK(status == VK_SUCCESS)
      << "vmaAllocateMemory failed with status code " << status;
  if (status != VK_SUCCESS)
    return nullptr;

  return fxl::AdoptRef(new VmaGpuMem(allocator_, allocation, allocation_info));
}

BufferPtr VmaGpuAllocator::AllocateBuffer(
    ResourceManager* manager, vk::DeviceSize size,
    vk::BufferUsageFlags usage_flags,
    vk::MemoryPropertyFlags memory_property_flags, GpuMemPtr* out_ptr) {
  vk::BufferCreateInfo info;
  info.size = size;
  info.usage = usage_flags;
  info.sharingMode = vk::SharingMode::eExclusive;

  // Needed so we can have a pointer to the C-style type.
  VkBufferCreateInfo c_buffer_info = info;

  VmaAllocationCreateInfo create_info = {
      VMA_ALLOCATION_CREATE_MAPPED_BIT,
      VMA_MEMORY_USAGE_UNKNOWN,
      static_cast<VkMemoryPropertyFlags>(memory_property_flags),
      0u,
      0u,
      VK_NULL_HANDLE,
      nullptr};

  if (out_ptr) {
    create_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }

  // Output structs.
  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo allocation_info;
  auto status = vmaCreateBuffer(allocator_, &c_buffer_info, &create_info,
                                &buffer, &allocation, &allocation_info);

  FXL_DCHECK(status == VK_SUCCESS)
      << "vmaAllocateMemory failed with status code " << status;
  if (status != VK_SUCCESS)
    return nullptr;

  auto retval = fxl::AdoptRef(
      new VmaBuffer(manager, allocator_, allocation, allocation_info, buffer));

  if (out_ptr) {
    FXL_DCHECK(allocation_info.offset == 0);
    *out_ptr = fxl::AdoptRef(new VmaMappedGpuMem(allocation_info, retval));
  }

  return retval;
}

ImagePtr VmaGpuAllocator::AllocateImage(ResourceManager* manager,
                                        const ImageInfo& info,
                                        GpuMemPtr* out_ptr) {
  // Needed so we have a pointer to the C-style type.
  VkImageCreateInfo c_image_info = image_utils::CreateVkImageCreateInfo(info);

  VmaAllocationCreateInfo create_info = {
      VMA_ALLOCATION_CREATE_MAPPED_BIT,
      VMA_MEMORY_USAGE_UNKNOWN,
      static_cast<VkMemoryPropertyFlags>(info.memory_flags),
      0u,
      0u,
      VK_NULL_HANDLE,
      nullptr};

  if (out_ptr) {
    create_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }

  // Output structs.
  VkImage image;
  VmaAllocation allocation;
  VmaAllocationInfo allocation_info;
  auto status = vmaCreateImage(allocator_, &c_image_info, &create_info, &image,
                               &allocation, &allocation_info);

  FXL_DCHECK(status == VK_SUCCESS)
      << "vmaAllocateMemory failed with status code " << status;
  if (status != VK_SUCCESS)
    return nullptr;

  auto retval = fxl::AdoptRef(new VmaImage(manager, info, image, allocator_,
                                           allocation, allocation_info));

  if (out_ptr) {
    FXL_DCHECK(allocation_info.offset == 0);
    *out_ptr = fxl::AdoptRef(new VmaMappedGpuMem(allocation_info, retval));
  }

  return retval;
}

uint32_t VmaGpuAllocator::GetTotalBytesAllocated() const {
  VmaStats stats;
  vmaCalculateStats(allocator_, &stats);
  return stats.total.usedBytes;
}

}  // namespace escher
