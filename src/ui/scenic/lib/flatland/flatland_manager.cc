// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>

namespace flatland {

FlatlandManager::FlatlandManager(
    const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
    const std::shared_ptr<Renderer>& renderer,
    const std::shared_ptr<UberStructSystem>& uber_struct_system,
    const std::shared_ptr<LinkSystem>& link_system,
    const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers)
    : flatland_presenter_(flatland_presenter),
      renderer_(renderer),
      uber_struct_system_(uber_struct_system),
      link_system_(link_system),
      buffer_collection_importers_(buffer_collection_importers) {}

void FlatlandManager::CreateFlatland(
    fidl::InterfaceRequest<fuchsia::ui::scenic::internal::Flatland> request) {
  const scheduling::SessionId id = uber_struct_system_->GetNextInstanceId();
  FX_DCHECK(flatland_instances_.find(id) == flatland_instances_.end());

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  FX_DCHECK(status == ZX_OK);

  auto flatland =
      std::make_unique<Flatland>(id, flatland_presenter_, renderer_, link_system_,
                                 uber_struct_system_->AllocateQueueForSession(id),
                                 buffer_collection_importers_, std::move(sysmem_allocator));

  auto result = flatland_instances_.emplace(
      id, std::make_unique<FlatlandInstance>(request.channel(), std::move(flatland)));
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->binding.Bind(std::move(request), instance->loop.dispatcher());

  // Run the waiter on the main thread, not the instance thread, so that it can clean up and join
  // the instance thread.
  status = instance->peer_closed_waiter.Begin(
      async_get_default_dispatcher(),
      [this, id](async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
                 const zx_packet_signal_t* signal) { this->RemoveFlatlandInstance(id); });
  FX_DCHECK(status == ZX_OK);

  const std::string name = "Flatland ID=" + std::to_string(id);
  status = instance->loop.StartThread(name.c_str());
  FX_DCHECK(status == ZX_OK);
}

size_t FlatlandManager::GetSessionCount() const { return flatland_instances_.size(); }

void FlatlandManager::RemoveFlatlandInstance(scheduling::SessionId session_id) {
  auto instance_kv = flatland_instances_.find(session_id);
  FX_DCHECK(instance_kv != flatland_instances_.end());

  // The fidl::Binding must be destroyed on the thread that owns the looper it is bound to. Remove
  // the instance from the map, then push cleanup onto the worker thread. Note that the closure
  // exists only to transfer the cleanup responsibilities to the worker thread.
  async::PostTask(instance_kv->second->loop.dispatcher(),
                  [instance = std::move(instance_kv->second)]() {});

  // Other resource cleanup can safely occur on the main thread.
  flatland_instances_.erase(session_id);
  uber_struct_system_->RemoveSession(session_id);
}

}  // namespace flatland
