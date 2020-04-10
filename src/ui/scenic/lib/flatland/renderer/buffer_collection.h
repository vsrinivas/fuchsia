// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_

#include <fuchsia/images/cpp/fidl.h>

#include <memory>

namespace flatland {

using BufferCollectionHandle = fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>;

// |BufferCollectionInfo| stores the information regarding a BufferCollection.
// Instantiated via calls to |New| below.
class BufferCollectionInfo {
 public:
  // Creates a new |BufferCollectionInfo| instance. The return value is null if the buffer was
  // not created successfully. This function sets the server-side sysmem image constraints.
  // TODO(48210): Make this an asynchronous call.
  // This operation is thread-safe as long as we do not use the same sysmem_allocator
  // across different threads simultaneously.
  static std::unique_ptr<BufferCollectionInfo> New(
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      BufferCollectionHandle buffer_collection_token);

  // Creates a non-initialized instance of this class. Fully initialized instances must
  // be created via a call to |New|.
  BufferCollectionInfo() = default;

  // Generates a token that is returned to the client, who can then use it to add additional
  // constraints on the collection. This must not be called after calling |WaitUntilAllocated|.
  BufferCollectionHandle GenerateToken() const;

  // This BufferCollectionInfo may not be allocated due to the fact that it may not necessarily
  // have all constraints set from every client with a token. As a result, this function waits on
  // all constraints to be set before returning, which may result in a hang. This is not meant to
  // be called on the render thread, however, but instead on the same thread as the Flatland
  // instance which called it, so that rendering of other instances is not impacted.
  //
  // Once this function successfully completes, no new tokens can be generated with a call to
  // GenerateToken() and no new constraints can be set.
  //
  // This function is thread-safe because |buffer_collection_ptr_|, which is a
  // SynchronousInterfacePtr, is thread-safe. This function will return false if the buffers are not
  // able to be constructed, for example if there are incompatible constraints that are set on the
  // server and client.
  bool WaitUntilAllocated();

  // Points to BufferCollection FIDL interface used to communicate with Sysmem.
  const fuchsia::sysmem::BufferCollectionSyncPtr& GetSyncPtr() const {
    return buffer_collection_ptr_;
  }

  // Info describing |buffer_collection_ptr|.
  const fuchsia::sysmem::BufferCollectionInfo_2& GetSysmemInfo() const {
    return buffer_collection_info_;
  }

 private:
  BufferCollectionInfo(fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_ptr,
                       fuchsia::sysmem::BufferCollectionTokenSyncPtr constraint_token)
      : buffer_collection_ptr_(std::move(buffer_collection_ptr)),
        constraint_token_(std::move(constraint_token)) {}

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_ptr_;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info_;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr constraint_token_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_BUFFER_COLLECTION_H_
