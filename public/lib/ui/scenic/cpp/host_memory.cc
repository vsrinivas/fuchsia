// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/cpp/host_memory.h"

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "lib/fxl/logging.h"
#include "lib/ui/scenic/cpp/fidl_helpers.h"

namespace scenic {
namespace {

// Returns true if a memory object is of an appropriate size to recycle.
bool CanReuseMemory(const HostMemory& memory, size_t desired_size) {
  return memory.data_size() >= desired_size &&
         memory.data_size() <= desired_size * 2;
}

std::pair<zx::vmo, fxl::RefPtr<HostData>> AllocateMemory(size_t size) {
  // Create the vmo and map it into this process.
  zx::vmo local_vmo;
  zx_status_t status = zx::vmo::create(size, 0u, &local_vmo);
  FXL_CHECK(status == ZX_OK) << "vmo create failed: status=" << status;
  auto data = fxl::MakeRefCounted<HostData>(local_vmo, 0u, size);

  // Drop rights before we transfer the VMO to the session manager.
  zx::vmo remote_vmo;
  status = local_vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                             &remote_vmo);
  FXL_CHECK(status == ZX_OK) << "replace rights failed: status=" << status;
  return std::make_pair(std::move(remote_vmo), std::move(data));
}

}  // namespace

HostData::HostData(const zx::vmo& vmo, off_t offset, size_t size,
                   uint32_t flags)
    : size_(size) {
  uintptr_t ptr;
  zx_status_t status =
      zx::vmar::root_self()->map(0, vmo, offset, size, flags, &ptr);
  FXL_CHECK(status == ZX_OK) << "map failed: status=" << status;
  ptr_ = reinterpret_cast<void*>(ptr);
}

HostData::~HostData() {
  zx_status_t status =
      zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(ptr_), size_);
  FXL_CHECK(status == ZX_OK) << "unmap failed: status=" << status;
}

HostMemory::HostMemory(Session* session, size_t size)
    : HostMemory(session, AllocateMemory(size)) {}

HostMemory::HostMemory(Session* session,
                       std::pair<zx::vmo, fxl::RefPtr<HostData>> init)
    : Memory(session, std::move(init.first),
             fuchsia::images::MemoryType::HOST_MEMORY),
      data_(std::move(init.second)) {}

HostMemory::HostMemory(HostMemory&& moved)
    : Memory(std::move(moved)), data_(std::move(moved.data_)) {}

HostMemory::~HostMemory() = default;

HostImage::HostImage(const HostMemory& memory, off_t memory_offset,
                     fuchsia::images::ImageInfo info)
    : HostImage(memory.session(), memory.id(), memory_offset, memory.data(),
                std::move(info)) {}

HostImage::HostImage(Session* session, uint32_t memory_id, off_t memory_offset,
                     fxl::RefPtr<HostData> data,
                     fuchsia::images::ImageInfo info)
    : Image(session, memory_id, memory_offset, std::move(info)),
      data_(std::move(data)) {}

HostImage::HostImage(HostImage&& moved)
    : Image(std::move(moved)), data_(std::move(moved.data_)) {}

HostImage::~HostImage() = default;

HostImagePool::HostImagePool(Session* session, uint32_t num_images)
    : session_(session), image_ptrs_(num_images), memory_ptrs_(num_images) {}

HostImagePool::~HostImagePool() = default;

// TODO(mikejurka): Double-check these changes
bool HostImagePool::Configure(const fuchsia::images::ImageInfo* image_info) {
  if (image_info) {
    if (configured_ && ImageInfoEquals(*image_info, image_info_)) {
      return false;  // no change
    }
    configured_ = true;
    image_info_ = *image_info;
  } else {
    if (!configured_) {
      return false;  // no change
    }
    configured_ = false;
  }

  for (uint32_t i = 0; i < num_images(); i++)
    image_ptrs_[i].reset();

  if (configured_) {
    FXL_DCHECK(image_info_.width > 0);
    FXL_DCHECK(image_info_.height > 0);
    FXL_DCHECK(image_info_.stride > 0);

    size_t desired_size = Image::ComputeSize(image_info_);
    for (uint32_t i = 0; i < num_images(); i++) {
      if (memory_ptrs_[i] && !CanReuseMemory(*memory_ptrs_[i], desired_size))
        memory_ptrs_[i].reset();
    }
  }
  return true;
}

const HostImage* HostImagePool::GetImage(uint32_t index) {
  FXL_DCHECK(index < num_images());

  if (image_ptrs_[index])
    return image_ptrs_[index].get();

  if (!configured_)
    return nullptr;

  if (!memory_ptrs_[index]) {
    memory_ptrs_[index] =
        std::make_unique<HostMemory>(session_, Image::ComputeSize(image_info_));
  }

  image_ptrs_[index] =
      std::make_unique<HostImage>(*memory_ptrs_[index], 0u, image_info_);
  return image_ptrs_[index].get();
}

void HostImagePool::DiscardImage(uint32_t index) {
  FXL_DCHECK(index < num_images());

  image_ptrs_[index].reset();
}

}  // namespace scenic
