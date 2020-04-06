// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_

#include <fuchsia/images/cpp/fidl.h>

#include <vulkan/vulkan.h>

#include "src/lib/fxl/logging.h"

#include <vulkan/vulkan.hpp>

namespace flatland {

using BufferCollectionHandle = fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>;

// |BufferCollectionInfo| stores the information regarding a BufferCollection.
// Instantiated via calls to |CreateBufferCollection| below. Calling the
// |DestroyBufferCollection| function below closes the SyncPtr and has the
// Vulkan device destroy the vk_buffer_handle.
class BufferCollectionInfo {
 public:
  // Creates a new |BufferCollectionInfo| instance. The return value is null if the buffer was
  // not created successfully. This function also sets the Vulkan constraints and the server-side
  // sysmem image constraints. The vulkan constraints are passed in via |vulkan_image_constraints|.
  // TODO(48210): Make this an asynchronous call.
  // This operation is thread-safe as long as we do not use the same sysmem_allocator
  // across different threads simultaneously.
  static std::unique_ptr<BufferCollectionInfo> CreateWithConstraints(
      const vk::Device& device, const vk::DispatchLoaderDynamic& vk_loader,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      const vk::ImageCreateInfo& vulkan_image_constraints,
      BufferCollectionHandle buffer_collection_token);

  // Creates a non-initialized instance of this class. Fully initialized instances must
  // be created via a call to |CreateWithConstraints|.
  BufferCollectionInfo() = default;

  ~BufferCollectionInfo() {
    // Guarantee that the vulkan buffer collection has been destroyed via a call
    // to |Destroy| before this instance goes out of scope.
    FXL_DCHECK(vk_buffer_collection_ == vk::BufferCollectionFUCHSIA());
  }

  // This BufferCollectionInfo may not be allocated due to the fact that it may not necessarily
  // have all constraints set from every client with a token. As a result, this function waits on
  // all constraints to be set before returning, which may result in a hang. This is not meant to
  // be called on the render thread, however, but instead on the same thread as the Flatland
  // instance which called it, so that rendering of other instances is not impacted.
  // This function is thread-safe because |buffer_collection_ptr_|, which is a
  // SynchronousInterfacePtr, is thread-safe. This function will return false if the buffers are not
  // able to be constructed, for example if there are incompatible constraints that are set on the
  // server and client.
  bool WaitUntilAllocated();

  // Closes the channel and destroys the vk buffer handle.
  // Should be called on the same thread that |info| was created due to FIDL binding restrictions.
  void Destroy(const vk::Device& device, const vk::DispatchLoaderDynamic& vk_loader);

  // Points to BufferCollection FIDL interface used to communicate with Sysmem.
  const fuchsia::sysmem::BufferCollectionSyncPtr& GetSyncPtr() const {
    return buffer_collection_ptr_;
  }

  // Used to set constraints on VkImage.
  const vk::BufferCollectionFUCHSIA& GetVkHandle() const { return vk_buffer_collection_; }

  // Info describing |buffer_collection_ptr|.
  const fuchsia::sysmem::BufferCollectionInfo_2& GetSysmemInfo() const {
    return buffer_collection_info_;
  }

 private:
  BufferCollectionInfo(fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_ptr,
                       vk::BufferCollectionFUCHSIA vk_buffer_collection)
      : buffer_collection_ptr_(std::move(buffer_collection_ptr)),
        vk_buffer_collection_(vk_buffer_collection) {}

  BufferCollectionInfo& operator=(const BufferCollectionInfo&) = delete;
  BufferCollectionInfo(const BufferCollectionInfo&) = delete;

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_ptr_;
  vk::BufferCollectionFUCHSIA vk_buffer_collection_;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_
