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
    const std::shared_ptr<UberStructSystem>& uber_struct_system,
    const std::shared_ptr<LinkSystem>& link_system,
    const std::vector<std::shared_ptr<BufferCollectionImporter>>& buffer_collection_importers)
    : flatland_presenter_(flatland_presenter),
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

  auto flatland = std::make_unique<Flatland>(
      id, flatland_presenter_, link_system_, uber_struct_system_->AllocateQueueForSession(id),
      buffer_collection_importers_, std::move(sysmem_allocator));

  auto result = flatland_instances_.emplace(
      id, std::make_unique<FlatlandInstance>(request.channel(), std::move(flatland)));
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->binding->Bind(std::move(request), instance->loop.dispatcher());

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
  // TODO(fxbug.dev/62669): add a present2_helper to each Flatland instance and trigger them from
  // here.
}

size_t FlatlandManager::GetSessionCount() const { return flatland_instances_.size(); }

void FlatlandManager::SendPresentTokens(FlatlandInstance* instance, uint32_t num_present_tokens) {
  // The FIDL binding must be accessed on the thread it is bound to. |instance| may be destroyed
  // before the task is dispatched, so capture a weak_ptr to the binding since the tokens do not
  // need to be returned when the instance is destroyed.
  std::weak_ptr<FlatlandInstance::ImplBinding> weak_binding = instance->binding;
  async::PostTask(instance->loop.dispatcher(), [weak_binding, num_present_tokens]() {
    // First return tokens to the instance, then return tokens to the client.
    if (auto binding = weak_binding.lock()) {
      binding->impl()->OnPresentTokensReturned(num_present_tokens);
      binding->events().OnPresentTokensReturned(num_present_tokens);
    }
  });
}

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
