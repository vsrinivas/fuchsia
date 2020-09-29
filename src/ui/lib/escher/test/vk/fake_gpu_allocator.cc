// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/vk/fake_gpu_allocator.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/util/image_utils.h"

namespace {

class FakeGpuMem : public escher::GpuMem {
 public:
  FakeGpuMem(vk::DeviceSize size, escher::test::FakeGpuAllocator* allocator)
      : GpuMem(vk::DeviceMemory(), size, 0, new uint8_t[size]), allocator_(allocator) {
    allocator_->OnAllocation(static_cast<uint64_t>(size));
  }

  ~FakeGpuMem() {
    allocator_->OnDeallocation(static_cast<uint64_t>(size()));
    delete[] mapped_ptr();
  }

 private:
  escher::test::FakeGpuAllocator* allocator_;
};

class FakeBuffer : public escher::Buffer {
 public:
  FakeBuffer(escher::ResourceManager* manager, vk::DeviceSize vk_buffer_size,
             const escher::GpuMemPtr& mem)
      : Buffer(manager, vk::Buffer(), vk_buffer_size, mem->mapped_ptr()), memory_(mem) {}

 private:
  escher::GpuMemPtr memory_;
};

class FakeImage : public escher::Image {
 public:
  FakeImage(escher::ResourceManager* manager, escher::ImageInfo info, const escher::GpuMemPtr& mem)
      : Image(manager, info, vk::Image(), mem->size(), mem->mapped_ptr(),
              vk::ImageLayout::eUndefined),
        memory_(mem) {}

 private:
  escher::GpuMemPtr memory_;
};

}  // namespace

namespace escher {
namespace test {

FakeGpuAllocator::FakeGpuAllocator() {}
FakeGpuAllocator::~FakeGpuAllocator() {}

GpuMemPtr FakeGpuAllocator::AllocateMemory(vk::MemoryRequirements reqs,
                                           vk::MemoryPropertyFlags flags) {
  return fxl::AdoptRef(new FakeGpuMem(reqs.size, this));
}

BufferPtr FakeGpuAllocator::AllocateBuffer(ResourceManager* manager, vk::DeviceSize size,
                                           vk::BufferUsageFlags usage_flags,
                                           vk::MemoryPropertyFlags memory_property_flags,
                                           GpuMemPtr* out_ptr) {
  auto memory = fxl::AdoptRef(new FakeGpuMem(size, this));
  FX_DCHECK(memory->size() >= size)
      << "Size of allocated memory should not be less than requested size";

  if (out_ptr)
    *out_ptr = memory;

  return fxl::AdoptRef(new FakeBuffer(manager, size, memory));
}

ImagePtr FakeGpuAllocator::AllocateImage(ResourceManager* manager, const ImageInfo& info,
                                         GpuMemPtr* out_ptr) {
  size_t bytes_per_pixel = image_utils::BytesPerPixel(info.format);
  size_t size = info.width * info.height * info.sample_count * bytes_per_pixel;

  auto memory = fxl::AdoptRef(new FakeGpuMem(size, this));

  if (out_ptr)
    *out_ptr = memory;

  return fxl::AdoptRef(new FakeImage(manager, info, memory));
}

size_t FakeGpuAllocator::GetTotalBytesAllocated() const { return bytes_allocated_; }

void FakeGpuAllocator::OnAllocation(uint64_t size) { bytes_allocated_ += size; }

void FakeGpuAllocator::OnDeallocation(uint64_t size) { bytes_allocated_ -= size; }

}  // namespace test
}  // namespace escher
