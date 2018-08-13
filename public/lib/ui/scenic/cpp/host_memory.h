// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_HOST_MEMORY_H_
#define LIB_UI_SCENIC_CPP_HOST_MEMORY_H_

#include <memory>
#include <utility>
#include <vector>

#include <lib/zx/vmo.h>

#include "lib/ui/scenic/cpp/resources.h"

namespace scenic {

// Provides access to data stored in a host-accessible shared memory region.
// The memory is unmapped once all references to this object have been released.
class HostData : public std::enable_shared_from_this<HostData> {
 public:
  // Maps a range of an existing VMO into memory.
  HostData(const zx::vmo& vmo, off_t offset, size_t size,
           uint32_t flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE |
                            ZX_VM_FLAG_MAP_RANGE);
  ~HostData();

  HostData(const HostData&) = delete;
  HostData& operator=(const HostData&) = delete;

  // Gets the size of the data in bytes.
  size_t size() const { return size_; }

  // Gets a pointer to the data.
  void* ptr() const { return ptr_; }

 private:
  size_t const size_;
  void* ptr_;
};

// Represents a host-accessible shared memory backed memory resource in a
// session.  The memory is mapped read/write into this process and transferred
// read-only to the scene manager.  The shared memory region is retained until
// this object is destroyed.
// TODO(MZ-268): Don't inherit from Memory, so that Memory can have a public
// move constructor.
class HostMemory final : public Memory {
 public:
  HostMemory(Session* session, size_t size);
  HostMemory(HostMemory&& moved);
  ~HostMemory();

  HostMemory(const HostMemory&) = delete;
  HostMemory& operator=(const HostMemory&) = delete;

  // Gets a reference to the underlying shared memory region.
  const std::shared_ptr<HostData>& data() const { return data_; }

  // Gets the size of the data in bytes.
  size_t data_size() const { return data_->size(); }

  // Gets a pointer to the data.
  void* data_ptr() const { return data_->ptr(); }

 private:
  explicit HostMemory(Session* session,
                      std::pair<zx::vmo, std::shared_ptr<HostData>> init);

  std::shared_ptr<HostData> data_;
};

// Represents an image resource backed by host-accessible shared memory bound to
// a session.  The shared memory region is retained until this object is
// destroyed.
// TODO(MZ-268): Don't inherit from Image, so that Image can have a public move
// constructor.
class HostImage final : public Image {
 public:
  HostImage(const HostMemory& memory, off_t memory_offset,
            fuchsia::images::ImageInfo info);
  HostImage(Session* session, uint32_t memory_id, off_t memory_offset,
            std::shared_ptr<HostData> data, fuchsia::images::ImageInfo info);
  HostImage(HostImage&& moved);
  ~HostImage();

  HostImage(const HostImage&) = delete;
  HostImage& operator=(const HostImage&) = delete;

  // Gets a reference to the underlying shared memory region.
  const std::shared_ptr<HostData>& data() const { return data_; }

  // Gets a pointer to the image data.
  void* image_ptr() const {
    return static_cast<uint8_t*>(data_->ptr()) + memory_offset();
  }

 private:
  std::shared_ptr<HostData> data_;
};

// Represents a pool of image resources backed by host-accessible shared memory
// bound to a session.  All images in the pool must have the same layout.
class HostImagePool {
 public:
  // Creates a pool which can supply up to |num_images| images on demand.
  explicit HostImagePool(Session* session, uint32_t num_images);
  ~HostImagePool();

  HostImagePool(const HostImagePool&) = delete;
  HostImagePool& operator=(const HostImagePool&) = delete;

  // The number of images which this pool can manage.
  uint32_t num_images() const { return image_ptrs_.size(); }

  // Gets information about the images in the pool, or nullptr if the
  // pool is not configured.
  const fuchsia::images::ImageInfo* image_info() const { return &image_info_; }

  // Sets the image information for images in the pool.
  // Previously created images are released but their memory may be reused.
  // If |image_info| is nullptr, the pool reverts to an non-configured state;
  // all images are released but the memory is retained for recycling.
  // Returns true if the configuration changed.
  bool Configure(const fuchsia::images::ImageInfo* image_info);

  // Gets the image with the specified index.
  // The |index| must be between 0 and |num_images() - 1|.
  // The returned pointer is valid until the image is discarded or the
  // pool is reconfigured.  Returns nullptr if the pool is not configured.
  const HostImage* GetImage(uint32_t index);

  // Discards the image with the specified index but recycles its memory.
  // The |index| must be between 0 and |num_images() - 1|.
  void DiscardImage(uint32_t index);

 private:
  Session* const session_;

  bool configured_ = false;
  fuchsia::images::ImageInfo image_info_;
  std::vector<std::unique_ptr<HostImage>> image_ptrs_;
  std::vector<std::unique_ptr<HostMemory>> memory_ptrs_;
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_HOST_MEMORY_H_
