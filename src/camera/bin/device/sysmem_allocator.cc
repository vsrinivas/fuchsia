// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/sysmem_allocator.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/syslog/cpp/macros.h>

namespace camera {
namespace {

constexpr uint32_t kNamePriority = 30;  // Higher than Scenic but below the maximum.

// Returns a promise that completes after calling |WaitForBuffersAllocated| on the provided
// BufferCollection.
//
// The |collection| is consumed by this operation and will be closed upon both success and failure.
fpromise::promise<BufferCollectionWithLifetime, zx_status_t> WaitForBuffersAllocated(
    fuchsia::sysmem::BufferCollectionPtr collection) {
  // Move the bridge completer into a shared_ptr so that we can share the completer between the
  // FIDL error handler and the WaitForBuffersAllocated callback.
  fpromise::bridge<BufferCollectionWithLifetime, zx_status_t> bridge;
  auto completer = std::make_shared<fpromise::completer<BufferCollectionWithLifetime, zx_status_t>>(
      std::move(bridge.completer));
  std::weak_ptr<fpromise::completer<BufferCollectionWithLifetime, zx_status_t>> weak_completer(
      completer);
  collection.set_error_handler([completer](zx_status_t status) {
    // After calling SetConstraints, allocation may fail. This results in WaitForBuffersAllocated
    // returning NO_MEMORY followed by channel closure. Because the client may observe these in
    // either order, treat channel closure as if it were NO_MEMORY.
    FX_CHECK(status != ZX_OK);
    completer->complete_error(status == ZX_ERR_PEER_CLOSED ? ZX_ERR_NO_MEMORY : status);
  });

  zx::eventpair deallocation_complete_client, deallocation_complete_server;
  zx::eventpair::create(/*options=*/0, &deallocation_complete_client,
                        &deallocation_complete_server);
  collection->AttachLifetimeTracking(std::move(deallocation_complete_server), 0);

  collection->WaitForBuffersAllocated(
      [weak_completer, deallocation_complete = std::move(deallocation_complete_client)](
          zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) mutable {
        auto completer = weak_completer.lock();
        if (completer) {
          if (status == ZX_OK) {
            BufferCollectionWithLifetime collection_lifetime;
            collection_lifetime.buffers = std::move(buffers);
            collection_lifetime.deallocation_complete = std::move(deallocation_complete);
            completer->complete_ok(std::move(collection_lifetime));
          } else {
            completer->complete_error(status);
          }
        }
      });
  return bridge.consumer.promise().inspect(
      [collection = std::move(collection)](
          const fpromise::result<BufferCollectionWithLifetime, zx_status_t>& result) mutable {
        if (collection) {
          collection->Close();
          collection = nullptr;
        }
      });
}

}  // namespace

SysmemAllocator::SysmemAllocator(fuchsia::sysmem::AllocatorHandle allocator)
    : allocator_(allocator.Bind()) {}

fpromise::promise<BufferCollectionWithLifetime, zx_status_t> SysmemAllocator::BindSharedCollection(
    fuchsia::sysmem::BufferCollectionTokenHandle token,
    fuchsia::sysmem::BufferCollectionConstraints constraints, std::string name) {
  TRACE_DURATION("camera", "SysmemAllocator::BindSharedCollection");
  // We expect sysmem to have free space, so bind the provided token now.
  fuchsia::sysmem::BufferCollectionPtr collection;
  allocator_->BindSharedCollection(std::move(token), collection.NewRequest());
  collection->SetName(kNamePriority, std::move(name));
  collection->SetConstraints(true, constraints);
  return WaitForBuffersAllocated(std::move(collection)).wrap_with(scope_);
}

}  // namespace camera
