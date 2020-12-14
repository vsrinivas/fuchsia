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
    async_dispatcher_t* dispatcher, const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
    const std::shared_ptr<UberStructSystem>& uber_struct_system,
    const std::shared_ptr<LinkSystem>& link_system,
    const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers)
    : flatland_presenter_(flatland_presenter),
      uber_struct_system_(uber_struct_system),
      link_system_(link_system),
      buffer_collection_importers_(buffer_collection_importers),
      executor_(dispatcher) {}

void FlatlandManager::CreateFlatland(
    fidl::InterfaceRequest<fuchsia::ui::scenic::internal::Flatland> request) {
  const scheduling::SessionId id = uber_struct_system_->GetNextInstanceId();
  FX_DCHECK(flatland_instances_.find(id) == flatland_instances_.end());

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator.NewRequest().TakeChannel().release());
  FX_DCHECK(status == ZX_OK);

  // Allocate the worker Loop first so that the Flatland impl can be bound to it's dispatcher.
  auto result = flatland_instances_.emplace(id, std::make_unique<FlatlandInstance>());
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->impl = std::make_shared<Flatland>(
      instance->loop.dispatcher(), std::move(request), id,
      std::bind(&FlatlandManager::DestroyInstanceFunction, this, id), flatland_presenter_,
      link_system_, uber_struct_system_->AllocateQueueForSession(id), buffer_collection_importers_,
      std::move(sysmem_allocator));

  const std::string name = "Flatland ID=" + std::to_string(id);
  status = instance->loop.StartThread(name.c_str());
  FX_DCHECK(status == ZX_OK);

  // TODO(fxbug.dev/44211): this logic may move into FrameScheduler
  // Send the client their initial allotment of present tokens minus one since clients assume they
  // start with one.
  SendPresentTokens(instance.get(), scheduling::FrameScheduler::kMaxPresentsInFlight - 1u);
}

scheduling::SessionUpdater::UpdateResults FlatlandManager::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  auto results = uber_struct_system_->UpdateSessions(sessions_to_update);

  // Return tokens to each session that didn't fail to update.
  for (const auto& [session_id, num_present_tokens] : results.present_tokens) {
    auto instance_kv = flatland_instances_.find(session_id);
    FX_DCHECK(instance_kv != flatland_instances_.end());

    SendPresentTokens(instance_kv->second.get(), num_present_tokens);
  }

  // TODO(fxbug.dev/62292): there shouldn't ever be sessions with failed updates, but if there
  // somehow are, those sessions should probably be closed.
  FX_DCHECK(results.scheduling_results.sessions_with_failed_updates.empty());

  return results.scheduling_results;
}

void FlatlandManager::OnFramePresented(
    const std::unordered_map<scheduling::SessionId,
                             std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  for (const auto& [session_id, latch_times] : latched_times) {
    auto instance_kv = flatland_instances_.find(session_id);

    // Skip sessions that have exited since their frame was rendered.
    if (instance_kv == flatland_instances_.end()) {
      continue;
    }

    SendFramePresented(instance_kv->second.get(), latch_times, present_times);
  }
}

size_t FlatlandManager::GetSessionCount() const { return flatland_instances_.size(); }

void FlatlandManager::SendPresentTokens(FlatlandInstance* instance, uint32_t num_present_tokens) {
  // The Flatland impl must be accessed on the thread it is bound to. |instance| may be destroyed
  // before the task is dispatched, so capture a weak_ptr to the impl since the tokens do not
  // need to be returned when the instance is destroyed.
  std::weak_ptr<Flatland> weak_impl = instance->impl;
  async::PostTask(instance->loop.dispatcher(), [weak_impl, num_present_tokens]() {
    if (auto impl = weak_impl.lock()) {
      impl->OnPresentTokensReturned(num_present_tokens);
    }
  });
}

void FlatlandManager::SendFramePresented(
    FlatlandInstance* instance,
    const std::map<scheduling::PresentId, /*latched_time*/ zx::time>& latched_times,
    scheduling::PresentTimestamps present_times) {
  // The Flatland impl must be accessed on the thread it is bound to. |instance| may be destroyed
  // before the task is dispatched, so capture a weak_ptr to the impl.
  std::weak_ptr<Flatland> weak_impl = instance->impl;
  async::PostTask(instance->loop.dispatcher(), [weak_impl, latched_times, present_times]() {
    if (auto impl = weak_impl.lock()) {
      impl->OnFramePresented(latched_times, present_times);
    }
  });
}

void FlatlandManager::RemoveFlatlandInstance(scheduling::SessionId session_id) {
  auto instance_kv = flatland_instances_.find(session_id);
  FX_DCHECK(instance_kv != flatland_instances_.end());

  // The Flatland impl must be destroyed on the thread that owns the looper it is bound to. Remove
  // the instance from the map, then push cleanup onto the worker thread. Note that the closure
  // exists only to transfer the cleanup responsibilities to the worker thread.
  async::PostTask(instance_kv->second->loop.dispatcher(),
                  [instance = std::move(instance_kv->second)]() {});

  // Other resource cleanup can safely occur on the main thread.
  flatland_instances_.erase(session_id);
  uber_struct_system_->RemoveSession(session_id);
}

void FlatlandManager::DestroyInstanceFunction(scheduling::SessionId session_id) {
  // This function is called on the Flatland instance thread, but the instance removal must be
  // triggered from the main thread since it accesses and modifies the |flatland_instances_| map.
  executor_.schedule_task(
      fit::make_promise([this, session_id] { this->RemoveFlatlandInstance(session_id); }));
}

}  // namespace flatland
