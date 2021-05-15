// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

namespace flatland {

FlatlandManager::FlatlandManager(
    async_dispatcher_t* dispatcher, const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
    const std::shared_ptr<UberStructSystem>& uber_struct_system,
    const std::shared_ptr<LinkSystem>& link_system,
    std::shared_ptr<scenic_impl::display::Display> display,
    const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
        buffer_collection_importers)
    : flatland_presenter_(flatland_presenter),
      uber_struct_system_(uber_struct_system),
      link_system_(link_system),
      buffer_collection_importers_(buffer_collection_importers),
      executor_(dispatcher),
      primary_display_(std::move(display)) {}

FlatlandManager::~FlatlandManager() {
  // Clean up externally managed resources.
  for (auto it = flatland_instances_.begin(); it != flatland_instances_.end();) {
    // Use post-increment because otherwise the iterator would be invalidated when the entry is
    // erased within RemoveFlatlandInstance().
    RemoveFlatlandInstance(it++->first);
  }
}

void FlatlandManager::CreateFlatland(
    fidl::InterfaceRequest<fuchsia::ui::scenic::internal::Flatland> request) {
  CheckIsOnMainThread();

  const scheduling::SessionId id = uber_struct_system_->GetNextInstanceId();
  FX_DCHECK(flatland_instances_.find(id) == flatland_instances_.end());

  // Allocate the worker Loop first so that the Flatland impl can be bound to its dispatcher.
  auto result = flatland_instances_.emplace(id, std::make_unique<FlatlandInstance>());
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->loop =
      std::make_shared<utils::LoopDispatcherHolder>(&kAsyncLoopConfigNoAttachToCurrentThread);
  instance->impl = Flatland::New(
      instance->loop, std::move(request), id,
      std::bind(&FlatlandManager::DestroyInstanceFunction, this, id), flatland_presenter_,
      link_system_, uber_struct_system_->AllocateQueueForSession(id), buffer_collection_importers_);

  const std::string name = "Flatland ID=" + std::to_string(id);
  zx_status_t status = instance->loop->loop().StartThread(name.c_str());
  FX_DCHECK(status == ZX_OK);

  // TODO(fxbug.dev/44211): this logic may move into FrameScheduler
  // Send the client their initial allotment of present tokens minus one since clients assume they
  // start with one. The client also receives information about the next 8 frames.
  //
  // `this` is safe to capture, as the callback is guaranteed to run on the calling thread.
  flatland_presenter_->GetFuturePresentationInfos(
      [this, id](std::vector<scheduling::FuturePresentationInfo> presentation_infos) {
        Flatland::FuturePresentationInfos infos;
        for (const auto& presentation_info : presentation_infos) {
          auto& info = infos.emplace_back();
          info.set_latch_point(presentation_info.latch_point.get());
          info.set_presentation_time(presentation_info.presentation_time.get());
        }
        // The Flatland instance may have been destroyed since the call was made.
        auto instance = flatland_instances_.find(id);
        if (instance != flatland_instances_.end()) {
          SendPresentTokens(instance->second.get(),
                            scheduling::FrameScheduler::kMaxPresentsInFlight - 1u,
                            std::move(infos));
        }
      });
}

scheduling::SessionUpdater::UpdateResults FlatlandManager::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  CheckIsOnMainThread();

  auto results = uber_struct_system_->UpdateSessions(sessions_to_update);

  // Prepares the return of tokens to each session that didn't fail to update.
  for (const auto& [session_id, num_present_tokens] : results.present_tokens) {
    auto instance_kv = flatland_instances_.find(session_id);
    FX_DCHECK(instance_kv != flatland_instances_.end());

    // Add the session to the map of updated_sessions, and increment the number of present tokens it
    // should receive after the firing of the OnCpuWorkDone() is issued from the scheduler.
    if (flatland_instances_updated_.find(session_id) == flatland_instances_updated_.end()) {
      flatland_instances_updated_[session_id] = 0;
    }
    flatland_instances_updated_[session_id] += num_present_tokens;
  }

  // TODO(fxbug.dev/62292): there shouldn't ever be sessions with failed updates, but if there
  // somehow are, those sessions should probably be closed.
  FX_DCHECK(results.scheduling_results.sessions_with_failed_updates.empty());

  return results.scheduling_results;
}

void FlatlandManager::OnCpuWorkDone() {
  CheckIsOnMainThread();

  // Get 8 frames of data, which we then pass onto all Flatland instances that had updates this
  // frame.
  //
  // `this` is safe to capture, as the callback is guaranteed to run on the calling thread.
  flatland_presenter_->GetFuturePresentationInfos(
      [this](std::vector<scheduling::FuturePresentationInfo> presentation_infos) {
        for (const auto& [session_id, num_present_tokens] : flatland_instances_updated_) {
          auto instance_kv = flatland_instances_.find(session_id);

          // Skip sessions that have exited since their frame was rendered.
          if (instance_kv == flatland_instances_.end()) {
            continue;
          }

          // Make a copy of the vector manually.
          Flatland::FuturePresentationInfos presentation_infos_copy(presentation_infos.size());
          for (size_t i = 0; i < presentation_infos.size(); ++i) {
            auto& info = presentation_infos[i];
            fuchsia::scenic::scheduling::PresentationInfo info_copy;
            info_copy.set_latch_point(info.latch_point.get());
            info_copy.set_presentation_time(info.presentation_time.get());
            presentation_infos_copy[i] = std::move(info_copy);
          }

          SendPresentTokens(instance_kv->second.get(), num_present_tokens,
                            std::move(presentation_infos_copy));
        }

        // Prepare map for the next frame.
        flatland_instances_updated_.clear();
      });
}

void FlatlandManager::OnFramePresented(
    const std::unordered_map<scheduling::SessionId,
                             std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  CheckIsOnMainThread();

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

void FlatlandManager::SendPresentTokens(FlatlandInstance* instance, uint32_t num_present_tokens,
                                        Flatland::FuturePresentationInfos presentation_infos) {
  CheckIsOnMainThread();

  // The Flatland impl must be accessed on the thread it is bound to; post a task to that thread.
  std::weak_ptr<Flatland> weak_impl = instance->impl;
  async::PostTask(instance->loop->dispatcher(),
                  [weak_impl, num_present_tokens,
                   presentation_infos = std::move(presentation_infos)]() mutable {
                    // |impl| is guaranteed to be non-null.  When destroying an instance, the
                    // manager erases the entry from the map, which means that subsequently
                    // |instance| would not be found to pass it to this method.
                    auto impl = weak_impl.lock();
                    FX_CHECK(impl) << "Missing Flatland instance in SendPresentTokens().";
                    impl->OnPresentProcessed(num_present_tokens, std::move(presentation_infos));
                  });
}

void FlatlandManager::SendFramePresented(
    FlatlandInstance* instance,
    const std::map<scheduling::PresentId, /*latched_time*/ zx::time>& latched_times,
    scheduling::PresentTimestamps present_times) {
  CheckIsOnMainThread();

  // The Flatland impl must be accessed on the thread it is bound to; post a task to that thread.
  std::weak_ptr<Flatland> weak_impl = instance->impl;
  async::PostTask(instance->loop->dispatcher(), [weak_impl, latched_times, present_times]() {
    // |impl| is guaranteed to be non-null.  When destroying an instance, the manager erases the
    // entry from the map, which means that subsequently |instance| would not be found to pass it to
    // this method.
    auto impl = weak_impl.lock();
    FX_CHECK(impl) << "Missing Flatland instance in SendFramePresented().";
    impl->OnFramePresented(latched_times, present_times);
  });
}

void FlatlandManager::RemoveFlatlandInstance(scheduling::SessionId session_id) {
  CheckIsOnMainThread();

  auto instance_kv = flatland_instances_.find(session_id);
  FX_DCHECK(instance_kv != flatland_instances_.end());

  // The Flatland impl must be destroyed on the thread that owns the looper it is bound to. Remove
  // the instance from the map, then push cleanup onto the worker thread. Note that the closure
  // exists only to transfer the cleanup responsibilities to the worker thread.
  async::PostTask(instance_kv->second->loop->dispatcher(),
                  [instance = std::move(instance_kv->second)]() {});

  // Other resource cleanup can safely occur on the main thread.
  flatland_instances_.erase(session_id);
  uber_struct_system_->RemoveSession(session_id);
  flatland_presenter_->RemoveSession(session_id);
}

void FlatlandManager::DestroyInstanceFunction(scheduling::SessionId session_id) {
  // This function is called on the Flatland instance thread, but the instance removal must be
  // triggered from the main thread since it accesses and modifies the |flatland_instances_| map.
  executor_.schedule_task(
      fit::make_promise([this, session_id] { this->RemoveFlatlandInstance(session_id); }));
}

}  // namespace flatland
