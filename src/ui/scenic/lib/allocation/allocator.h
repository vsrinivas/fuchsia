// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ALLOCATION_ALLOCATOR_H_
#define SRC_UI_SCENIC_LIB_ALLOCATION_ALLOCATOR_H_

#include <fuchsia/scenic/allocation/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <unordered_set>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/utils/post_initialization_runner.h"

namespace allocation {

// This class implements Allocator service which allows allocation of BufferCollections which can be
// used in multiple Flatland/Gfx sessions simultaneously.
class Allocator : public fuchsia::scenic::allocation::Allocator {
 public:
  Allocator(
      sys::ComponentContext* app_context,
      const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers,
      fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator);
  ~Allocator() override;

  void SetInitialized(const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
                          buffer_collection_importers);

  // |fuchsia::scenic::allocation::Allocator|
  void RegisterBufferCollection(
      fuchsia::scenic::allocation::BufferCollectionExportToken export_token,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> buffer_collection_token,
      RegisterBufferCollectionCallback callback) override;

 private:
  void CreateAllocator(fidl::InterfaceRequest<fuchsia::scenic::allocation::Allocator> request);

  void ReleaseBufferCollection(GlobalBufferCollectionId collection_id);

  // Dispatcher where this class runs on. Currently points to scenic main thread's dispatcher.
  async_dispatcher_t* dispatcher_;

  // Used for queuing tasks until Initialize() is called.
  utils::PostInitializationRunner post_initialization_runner_;

  // The FIDL bindings for this Allocator instance, which reference |this| as the implementation and
  // run on |dispatcher_|.
  fidl::BindingSet<fuchsia::scenic::allocation::Allocator> bindings_;

  // Used to import Flatland buffer collections and images to external services that Flatland does
  // not have knowledge of. Each importer is used for a different service.
  std::vector<std::shared_ptr<BufferCollectionImporter>> buffer_collection_importers_;

  // A Sysmem allocator to facilitate buffer allocation with the Renderer.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  // Keep track of buffer collection Ids for garbage collection.
  std::unordered_set<GlobalBufferCollectionId> buffer_collections_;

  // Should be last.
  fxl::WeakPtrFactory<Allocator> weak_factory_;
};

}  // namespace allocation

#endif  // SRC_UI_SCENIC_LIB_ALLOCATION_ALLOCATOR_H_
