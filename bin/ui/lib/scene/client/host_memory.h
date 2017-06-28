// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <mx/vmo.h>

#include "apps/mozart/lib/scene/client/resources.h"
#include "lib/ftl/memory/ref_counted.h"

namespace mozart {
namespace client {

// Provides access to data stored in a host-accessible shared memory region.
// The memory is unmapped once all references to this object have been released.
class HostData : public ftl::RefCountedThreadSafe<HostData> {
 public:
  // Maps a range of an existing VMO into memory.
  HostData(const mx::vmo& vmo,
           off_t offset,
           size_t size,
           uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                            MX_VM_FLAG_MAP_RANGE);

  // Gets the size of the data in bytes.
  size_t size() const { return size_; }

  // Gets a pointer to the data.
  void* ptr() const { return ptr_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(HostData);
  ~HostData();

  size_t const size_;
  void* ptr_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostData);
};

// Represents a host-accessible shared memory backed memory resource in a
// session.  The memory is mapped read/write into this process and transferred
// read-only to the scene manager.  The shared memory region is retained until
// this object is destroyed.
class HostMemory : public Memory {
 public:
  explicit HostMemory(Session* session, size_t size);
  ~HostMemory();

  // Gets a reference to the underlying shared memory region.
  const ftl::RefPtr<HostData>& data() const { return data_; }

  // Gets the size of the data in bytes.
  size_t data_size() const { return data_->size(); }

  // Gets a pointer to the data.
  void* data_ptr() const { return data_->ptr(); }

 private:
  explicit HostMemory(Session* session,
                      std::pair<mx::vmo, ftl::RefPtr<HostData>> init);

  ftl::RefPtr<HostData> data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostMemory);
};

// Represents an image resource backed by host-accessible shared memory bound to
// a session.  The shared memory region is retained until this object is
// destroyed.
class HostImage : public Image {
 public:
  explicit HostImage(const HostMemory& memory,
                     off_t memory_offset,
                     mozart2::ImageInfoPtr info);
  explicit HostImage(Session* session,
                     uint32_t memory_id,
                     off_t memory_offset,
                     ftl::RefPtr<HostData> data,
                     mozart2::ImageInfoPtr info);
  ~HostImage();

  // Gets a reference to the underlying shared memory region.
  const ftl::RefPtr<HostData>& data() const { return data_; }

  // Gets a pointer to the image data.
  void* image_ptr() const {
    return static_cast<uint8_t*>(data_->ptr()) + memory_offset();
  }

 private:
  ftl::RefPtr<HostData> data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostImage);
};

// Represents a pool of image resources backed by host-accessible shared memory
// bound to a session.  All images in the pool must have the same layout.
class HostImagePool {
 public:
  // Creates a pool which can supply up to |num_images| images on demand.
  explicit HostImagePool(Session* session, uint32_t num_images = 2u);
  ~HostImagePool();

  // The number of images which this pool can manage.
  uint32_t num_images() const { return image_ptrs_.size(); }

  // Gets information about the images in the pool, or nullptr if the
  // pool is not configured.
  const mozart2::ImageInfo* image_info() const { return image_info_.get(); }

  // Sets the image information for images in the pool.
  // Previously created images are released but their memory may be reused.
  // If |image_info| is nullptr, the pool reverts to an non-configured state;
  // all images are released but the memory is retained for recycling.
  // Returns true if the configuration changed.
  bool Configure(mozart2::ImageInfoPtr image_info);

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

  mozart2::ImageInfoPtr image_info_;
  std::vector<std::unique_ptr<HostImage>> image_ptrs_;
  std::vector<std::unique_ptr<HostMemory>> memory_ptrs_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HostImagePool);
};

}  // namespace client
}  // namespace mozart
