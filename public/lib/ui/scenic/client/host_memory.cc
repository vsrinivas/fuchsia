// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/client/host_memory.h"

#include <mx/vmar.h>
#include <mx/vmo.h>

#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ftl/logging.h"

namespace scenic_lib {
namespace {

// Returns true if a memory object is of an appropriate size to recycle.
bool CanReuseMemory(const HostMemory& memory, size_t desired_size) {
  return memory.data_size() >= desired_size &&
         memory.data_size() <= desired_size * 2;
}

std::pair<mx::vmo, ftl::RefPtr<HostData>> AllocateMemory(size_t size) {
  // Create the vmo and map it into this process.
  mx::vmo local_vmo;
  mx_status_t status = mx::vmo::create(size, 0u, &local_vmo);
  FTL_CHECK(status == MX_OK) << "vmo create failed: status=" << status;
  auto data = ftl::MakeRefCounted<HostData>(local_vmo, 0u, size);

  // Drop rights before we transfer the VMO to the session manager.
  mx::vmo remote_vmo;
  status = local_vmo.replace(
      MX_RIGHT_READ | MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_MAP,
      &remote_vmo);
  FTL_CHECK(status == MX_OK) << "replace rights failed: status=" << status;
  return std::make_pair(std::move(remote_vmo), std::move(data));
}

}  // namespace

HostData::HostData(const mx::vmo& vmo,
                   off_t offset,
                   size_t size,
                   uint32_t flags)
    : size_(size) {
  uintptr_t ptr;
  mx_status_t status =
      mx::vmar::root_self().map(0, vmo, offset, size, flags, &ptr);
  FTL_CHECK(status == MX_OK) << "map failed: status=" << status;
  ptr_ = reinterpret_cast<void*>(ptr);
}

HostData::~HostData() {
  mx_status_t status =
      mx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(ptr_), size_);
  FTL_CHECK(status == MX_OK) << "unmap failed: status=" << status;
}

HostMemory::HostMemory(Session* session, size_t size)
    : HostMemory(session, AllocateMemory(size)) {}

HostMemory::HostMemory(Session* session,
                       std::pair<mx::vmo, ftl::RefPtr<HostData>> init)
    : Memory(session, std::move(init.first), scenic::MemoryType::HOST_MEMORY),
      data_(std::move(init.second)) {}

HostMemory::HostMemory(HostMemory&& moved)
    : Memory(std::move(moved)), data_(std::move(moved.data_)) {}

HostMemory::~HostMemory() = default;

HostImage::HostImage(const HostMemory& memory,
                     off_t memory_offset,
                     scenic::ImageInfoPtr info)
    : HostImage(memory.session(),
                memory.id(),
                memory_offset,
                memory.data(),
                std::move(info)) {}

HostImage::HostImage(Session* session,
                     uint32_t memory_id,
                     off_t memory_offset,
                     ftl::RefPtr<HostData> data,
                     scenic::ImageInfoPtr info)
    : Image(session, memory_id, memory_offset, std::move(info)),
      data_(std::move(data)) {}

HostImage::HostImage(HostImage&& moved)
    : Image(std::move(moved)), data_(std::move(moved.data_)) {}

HostImage::~HostImage() = default;

HostImagePool::HostImagePool(Session* session, uint32_t num_images)
    : session_(session), image_ptrs_(num_images), memory_ptrs_(num_images) {}

HostImagePool::~HostImagePool() = default;

bool HostImagePool::Configure(const scenic::ImageInfo* image_info) {
  if (image_info) {
    if (image_info_ && image_info->Equals(*image_info_.get()))
      return false;  // no change
    if (!image_info_)
      image_info_ = image_info->Clone();
    else
      *image_info_ = *image_info;
  } else {
    if (!image_info_)
      return false;  // no change
    image_info_.reset();
  }

  for (uint32_t i = 0; i < num_images(); i++)
    image_ptrs_[i].reset();

  if (image_info_) {
    FTL_DCHECK(image_info_->width > 0);
    FTL_DCHECK(image_info_->height > 0);
    FTL_DCHECK(image_info_->stride > 0);

    size_t desired_size = Image::ComputeSize(*image_info_);
    for (uint32_t i = 0; i < num_images(); i++) {
      if (memory_ptrs_[i] && !CanReuseMemory(*memory_ptrs_[i], desired_size))
        memory_ptrs_[i].reset();
    }
  }
  return true;
}

const HostImage* HostImagePool::GetImage(uint32_t index) {
  FTL_DCHECK(index < num_images());

  if (image_ptrs_[index])
    return image_ptrs_[index].get();

  if (!image_info_)
    return nullptr;

  if (!memory_ptrs_[index]) {
    memory_ptrs_[index] = std::make_unique<HostMemory>(
        session_, Image::ComputeSize(*image_info_));
  }

  image_ptrs_[index] = std::make_unique<HostImage>(*memory_ptrs_[index], 0u,
                                                   image_info_.Clone());
  return image_ptrs_[index].get();
}

void HostImagePool::DiscardImage(uint32_t index) {
  FTL_DCHECK(index < num_images());

  image_ptrs_[index].reset();
}

}  // namespace scenic_lib
