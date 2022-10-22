// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/flatland_manager.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/trace/event.h>

#include <utility>

#include "lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace flatland {

FlatlandManager::FlatlandManager(
    async_dispatcher_t* dispatcher, const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
    const std::shared_ptr<UberStructSystem>& uber_struct_system,
    const std::shared_ptr<LinkSystem>& link_system,
    std::shared_ptr<scenic_impl::display::Display> display,
    std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers,
    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
        register_view_focuser,
    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
        register_view_ref_focused,
    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
        register_touch_source,
    fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
        register_mouse_source)
    : flatland_presenter_(flatland_presenter),
      uber_struct_system_(uber_struct_system),
      link_system_(link_system),
      buffer_collection_importers_(std::move(buffer_collection_importers)),
      executor_(dispatcher),
      primary_display_(std::move(display)),
      register_view_focuser_(std::move(register_view_focuser)),
      register_view_ref_focused_(std::move(register_view_ref_focused)),
      register_touch_source_(std::move(register_touch_source)),
      register_mouse_source_(std::move(register_mouse_source)) {
  FX_DCHECK(dispatcher);
  FX_DCHECK(flatland_presenter_);
  FX_DCHECK(uber_struct_system_);
  FX_DCHECK(link_system_);
  FX_DCHECK(register_view_focuser_);
  FX_DCHECK(register_view_ref_focused_);
  FX_DCHECK(register_touch_source_);
  FX_DCHECK(register_mouse_source_);
#ifndef NDEBUG
  for (auto& buffer_collection_importer : buffer_collection_importers_) {
    FX_DCHECK(buffer_collection_importer);
  }
#endif
}

FlatlandManager::~FlatlandManager() {
  // Clean up externally managed resources.
  for (auto it = flatland_instances_.begin(); it != flatland_instances_.end();) {
    // Use post-increment because otherwise the iterator would be invalidated when the entry is
    // erased within RemoveFlatlandInstance().
    RemoveFlatlandInstance(it++->first);
  }

  // Destroy the flatland manager only after all flatland instances have been destroyed on their
  // worker threads.
  while (alive_sessions_ > 0) {
    std::this_thread::yield();
  }
}

void FlatlandManager::CreateFlatland(
    fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request) {
  CheckIsOnMainThread();

  const scheduling::SessionId id = uber_struct_system_->GetNextInstanceId();
  FX_DCHECK(flatland_instances_.find(id) == flatland_instances_.end());
  FX_DCHECK(flatland_display_instances_.find(id) == flatland_display_instances_.end());

  zx_koid_t endpoint_id;
  zx_koid_t peer_endpoint_id;
  std::tie(endpoint_id, peer_endpoint_id) = fsl::GetKoids(request.channel().get());

  const std::string name =
      "Flatland ID=" + std::to_string(id) + " PEER=" + std::to_string(peer_endpoint_id);

  // Allocate the worker Loop first so that the Flatland impl can be bound to its dispatcher.
  auto result = flatland_instances_.emplace(id, std::make_unique<FlatlandInstance>());
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->loop =
      std::make_shared<utils::LoopDispatcherHolder>(&kAsyncLoopConfigNoAttachToCurrentThread);
  instance->impl = NewFlatland(
      instance->loop, std::move(request), id,
      std::bind(&FlatlandManager::DestroyInstanceFunction, this, id), flatland_presenter_,
      link_system_, uber_struct_system_->AllocateQueueForSession(id), buffer_collection_importers_);

  zx_status_t status = instance->loop->loop().StartThread(name.c_str());
  FX_DCHECK(status == ZX_OK);

  alive_sessions_++;
}

std::shared_ptr<Flatland> FlatlandManager::NewFlatland(
    std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
    fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
    scheduling::SessionId session_id, std::function<void()> destroy_instance_function,
    std::shared_ptr<FlatlandPresenter> flatland_presenter, std::shared_ptr<LinkSystem> link_system,
    std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
    const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
        buffer_collection_importers) const {
  return Flatland::New(
      std::move(dispatcher_holder), std::move(request), session_id,
      std::move(destroy_instance_function), std::move(flatland_presenter), std::move(link_system),
      std::move(uber_struct_queue), std::move(buffer_collection_importers),
      // All the register callbacks will be called on the instance thread, so we
      // must make sure to post the work back on the main thread.
      /*register_view_focuser*/
      [this](fidl::InterfaceRequest<fuchsia::ui::views::Focuser> focuser, zx_koid_t view_ref_koid) {
        async::PostTask(executor_.dispatcher(),
                        [this, focuser = std::move(focuser), view_ref_koid]() mutable {
                          TRACE_DURATION("gfx", "FlatlandManager::NewFlatland[Focuser]");
                          CheckIsOnMainThread();
                          register_view_focuser_(std::move(focuser), view_ref_koid);
                        });
      },
      /*register_view_ref_focused*/
      [this](fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> view_ref_focused,
             zx_koid_t view_ref_koid) {
        async::PostTask(
            executor_.dispatcher(),
            [this, view_ref_focused = std::move(view_ref_focused), view_ref_koid]() mutable {
              TRACE_DURATION("gfx", "FlatlandManager::NewFlatland[ViewRefFocused]");
              CheckIsOnMainThread();
              register_view_ref_focused_(std::move(view_ref_focused), view_ref_koid);
            });
      },
      /*register_touch_source*/
      [this](fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource> touch_source,
             zx_koid_t view_ref_koid) {
        async::PostTask(executor_.dispatcher(),
                        [this, touch_source = std::move(touch_source), view_ref_koid]() mutable {
                          TRACE_DURATION("gfx", "FlatlandManager::NewFlatland[TouchSource]");
                          CheckIsOnMainThread();
                          register_touch_source_(std::move(touch_source), view_ref_koid);
                        });
      },
      /*register_mouse_source*/
      [this](fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource> mouse_source,
             zx_koid_t view_ref_koid) {
        async::PostTask(executor_.dispatcher(),
                        [this, mouse_source = std::move(mouse_source), view_ref_koid]() mutable {
                          TRACE_DURATION("gfx", "FlatlandManager::NewFlatland[MouseSource]");
                          CheckIsOnMainThread();
                          register_mouse_source_(std::move(mouse_source), view_ref_koid);
                        });
      });
}

void FlatlandManager::CreateFlatlandDisplay(
    fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> request) {
  const scheduling::SessionId id = uber_struct_system_->GetNextInstanceId();
  FX_DCHECK(flatland_instances_.find(id) == flatland_instances_.end());
  FX_DCHECK(flatland_display_instances_.find(id) == flatland_display_instances_.end());

  // TODO(fxbug.dev/76985): someday there will be a DisplayToken or something for the client to
  // identify which hardware display this FlatlandDisplay is associated with.  For now: hard-coded.
  auto hw_display = primary_display_;

  if (hw_display->is_claimed()) {
    // TODO(fxbug.dev/76640): error reporting direct to client somehow?
    FX_LOGS(ERROR) << "Display id=" << hw_display->display_id()
                   << " is already claimed, cannot instantiate FlatlandDisplay.";
    return;
  }
  hw_display->Claim();

  // Allocate the worker Loop first so that the impl can be bound to its dispatcher.
  auto result =
      flatland_display_instances_.emplace(id, std::make_unique<FlatlandDisplayInstance>());
  FX_DCHECK(result.second);

  auto& instance = result.first->second;
  instance->loop =
      std::make_shared<utils::LoopDispatcherHolder>(&kAsyncLoopConfigNoAttachToCurrentThread);
  instance->display = hw_display;
  instance->impl = FlatlandDisplay::New(
      instance->loop, std::move(request), id, hw_display,
      std::bind(&FlatlandManager::DestroyInstanceFunction, this, id), flatland_presenter_,
      link_system_, uber_struct_system_->AllocateQueueForSession(id));

  const std::string name = "Flatland Display ID=" + std::to_string(id);
  zx_status_t status = instance->loop->loop().StartThread(name.c_str());
  FX_DCHECK(status == ZX_OK);

  link_system_->set_initial_device_pixel_ratio(hw_display->device_pixel_ratio());

  alive_sessions_++;
}

scheduling::SessionUpdater::UpdateResults FlatlandManager::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  CheckIsOnMainThread();

  auto results = uber_struct_system_->UpdateSessions(sessions_to_update);

  // Prepares the return of tokens to each session that didn't fail to update.
  for (const auto& [session_id, present_credits_returned] : results.present_credits_returned) {
    auto instance_kv = flatland_instances_.find(session_id);
    FX_DCHECK((flatland_instances_.find(session_id) != flatland_instances_.end()) ||
              (flatland_display_instances_.find(session_id) != flatland_display_instances_.end()));

    // TODO(fxbug.dev/76640): we currently only keep track of present tokens for Flatland sessions,
    // not FlatlandDisplay sessions.  It's not clear what we could do with them for FlatlandDisplay:
    // there is no API that would allow sending them to the client.  Maybe the current approach is
    // OK?  Maybe we should DCHECK that |present_credits_returned| is only non-zero for Flatlands,
    // not FlatlandDisplays?

    // Add the session to the map of updated_sessions, and increment the number of present tokens it
    // should receive after the firing of the OnCpuWorkDone() is issued from the scheduler.
    if (flatland_instances_updated_.find(session_id) == flatland_instances_updated_.end()) {
      flatland_instances_updated_[session_id] = 0;
    }
    flatland_instances_updated_[session_id] += present_credits_returned;
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
  const std::vector<scheduling::FuturePresentationInfo> presentation_infos =
      flatland_presenter_->GetFuturePresentationInfos();
  for (const auto& [session_id, present_credits_returned] : flatland_instances_updated_) {
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

    // The first time we send credits we should send the maximum amount for the client to get
    // started.
    uint32_t credits_returned = present_credits_returned;
    if (!instance_kv->second->initial_credits_returned) {
      credits_returned = scheduling::FrameScheduler::kMaxPresentsInFlight;
      instance_kv->second->initial_credits_returned = true;
    }

    SendPresentCredits(instance_kv->second.get(), credits_returned,
                       std::move(presentation_infos_copy));
  }

  // Prepare map for the next frame.
  flatland_instances_updated_.clear();
}

void FlatlandManager::OnFramePresented(
    const std::unordered_map<scheduling::SessionId,
                             std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  TRACE_DURATION("gfx", "FlatlandManager::OnFramePresented");

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

void FlatlandManager::SendPresentCredits(FlatlandInstance* instance,
                                         uint32_t present_credits_returned,
                                         Flatland::FuturePresentationInfos presentation_infos) {
  CheckIsOnMainThread();

  // The Flatland impl must be accessed on the thread it is bound to; post a task to that thread.
  std::weak_ptr<Flatland> weak_impl = instance->impl;
  async::PostTask(instance->loop->dispatcher(),
                  [weak_impl, present_credits_returned,
                   presentation_infos = std::move(presentation_infos)]() mutable {
                    // |impl| is guaranteed to be non-null.  When destroying an instance, the
                    // manager erases the entry from the map, which means that subsequently
                    // |instance| would not be found to pass it to this method.
                    auto impl = weak_impl.lock();
                    FX_CHECK(impl) << "Missing Flatland instance in SendPresentCredits().";
                    impl->OnNextFrameBegin(present_credits_returned, std::move(presentation_infos));
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

  bool found = false;

  {
    auto instance_kv = flatland_instances_.find(session_id);
    if (instance_kv != flatland_instances_.end()) {
      found = true;
      // The Flatland impl must be destroyed on the thread that owns the looper it is bound to.
      // Remove the instance from the map, then push cleanup onto the worker thread. Note that the
      // closure exists only to transfer the cleanup responsibilities to the worker thread.
      //
      // Note: Capturing "this" is safe as a flatland manager is guaranteed to outlive any flatland
      // instance.
      async::PostTask(instance_kv->second->loop->dispatcher(),
                      [instance = std::move(instance_kv->second), this]() mutable {
                        // A flatland instance must release all its resources before
                        // |alive_sessions_| is decremented. This ensures that flatland manager is
                        // not destroyed before the flatland instance.
                        instance->impl.reset();
                        alive_sessions_--;
                      });
      flatland_instances_.erase(session_id);
    }
  }
  {
    auto instance_kv = flatland_display_instances_.find(session_id);
    if (instance_kv != flatland_display_instances_.end()) {
      found = true;
      // Below, we push destruction of the object to a different thread.  But first, we need to
      // relinquish ownership of the display.
      instance_kv->second->display->Unclaim();

      // The Flatland impl must be destroyed on the thread that owns the looper it is
      // bound to. Remove the instance from the map, then push cleanup onto the worker thread. Note
      // that the closure exists only to transfer the cleanup responsibilities to the worker thread.
      //
      // Note: Capturing "this" is safe as a flatland manager is guaranteed to outlive any flatland
      // display instance.
      async::PostTask(instance_kv->second->loop->dispatcher(),
                      [instance = std::move(instance_kv->second), this]() mutable {
                        TRACE_DURATION("gfx", "FlatlandManager::RemoveFlatlandInstance[task]");

                        // A flatland display instance must release all its resources before
                        // |alive_sessions_| is decremented. This ensures that flatland manager is
                        // not destroyed before the flatland display instance.
                        instance->impl.reset();
                        alive_sessions_--;
                      });
      flatland_display_instances_.erase(session_id);
    }
  }
  FX_DCHECK(found) << "No instance or display with ID: " << session_id;

  // Other resource cleanup can safely occur on the main thread.
  uber_struct_system_->RemoveSession(session_id);
  flatland_presenter_->RemoveSession(session_id);
}

void FlatlandManager::DestroyInstanceFunction(scheduling::SessionId session_id) {
  // This function is called on the Flatland instance thread, but the instance removal must be
  // triggered from the main thread since it accesses and modifies the |flatland_instances_| map.
  executor_.schedule_task(
      fpromise::make_promise([this, session_id] { this->RemoveFlatlandInstance(session_id); }));
}

std::shared_ptr<FlatlandDisplay> FlatlandManager::GetPrimaryFlatlandDisplayForRendering() {
  FX_CHECK(flatland_display_instances_.size() <= 1);
  return flatland_display_instances_.empty() ? nullptr
                                             : flatland_display_instances_.begin()->second->impl;
}

}  // namespace flatland
