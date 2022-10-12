// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>

#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/flatland_display.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

namespace flatland {

class FlatlandManager : public scheduling::SessionUpdater {
 public:
  FlatlandManager(
      async_dispatcher_t* dispatcher, const std::shared_ptr<FlatlandPresenter>& flatland_presenter,
      const std::shared_ptr<UberStructSystem>& uber_struct_system,
      const std::shared_ptr<LinkSystem>& link_system,
      std::shared_ptr<scenic_impl::display::Display> display,
      std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>
          buffer_collection_importers,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
          register_view_focuser,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
          register_view_ref_focused,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
          register_touch_source,
      fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
          register_mouse_source);
  ~FlatlandManager() override;

  void CreateFlatland(fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> flatland);

  // TODO(fxbug.dev/76985): this creates a FlatlandDisplay attached to the "primary" hardware
  // display (i.e. the only one supported).  In the future there will be APIs that allow clients to
  // enumerate the displays, and to allow creation of a FlatlandDisplay bound to a specific one of
  // them.  For now, attempts to create a second concurrent FlatlandDisplay (or
  // gfx::DisplayCompositor) will fail.
  void CreateFlatlandDisplay(
      fidl::InterfaceRequest<fuchsia::ui::composition::FlatlandDisplay> flatland);

  // |scheduling::SessionUpdater|
  scheduling::SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;

  // |scheduling::SessionUpdater|
  void OnCpuWorkDone() override;

  // |scheduling::SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<scheduling::SessionId,
                               std::map<scheduling::PresentId, /*latched_time*/ zx::time>>&
          latched_times,
      scheduling::PresentTimestamps present_times) override;

  // For validating test logic.
  // Sessions still "owned" by FlatlandManager.
  size_t GetSessionCount() const;
  // Sessions still alive (but which might have been remove from FlatlandManager).
  size_t GetAliveSessionCount() const { return alive_sessions_; }

  // TODO(fxbug.dev/76985): This is a temporary method assuming that there are either 0 or 1
  // displays with attached Flatland content.  Used by TemporaryFrameRendererDelegator: if no
  // FlatlandDisplay is found, then it assumes that there might be "Gfx" content to render.
  std::shared_ptr<FlatlandDisplay> GetPrimaryFlatlandDisplayForRendering();

 private:
  // Represents an individual Flatland session for a client.
  struct FlatlandInstance {
    // The looper for this Flatland instance, which will be run on a worker thread spawned by the
    // async::Loop itself. It must be the first member of this struct so that |impl| is
    // destroyed first in the default destruction order, else it will attempt to run on a shutdown
    // looper.
    std::shared_ptr<utils::LoopDispatcherHolder> loop;

    // The implementation of Flatland, which includes the bindings for the instance. This must come
    // before |peer_closed_waiter| so that the Wait is destroyed, and therefore cancelled, before
    // the impl is destroyed in the default destruction order.
    std::shared_ptr<Flatland> impl;

    // Tracks the first time we return credits to the client.
    bool initial_credits_returned = false;
  };

  // TODO(fxbug.dev/76640): there's quite some overlap with FlatlandInstance.  Can they be unified?
  // Perhaps name the unified struct "FlatlandApiInstance"?
  struct FlatlandDisplayInstance {
    // The looper for this Flatland display, which will be run on a worker thread spawned by the
    // async::Loop itself. It must be the first member of this struct so that |impl| is
    // destroyed first in the default destruction order, else it will attempt to run on a shutdown
    // looper.
    std::shared_ptr<utils::LoopDispatcherHolder> loop;

    std::shared_ptr<FlatlandDisplay> impl;
    std::shared_ptr<scenic_impl::display::Display> display;
  };

  // Sends |additional_present_credits| to a particular Flatland |instance|.
  void SendPresentCredits(FlatlandInstance* instance, uint32_t additional_present_credits,
                          Flatland::FuturePresentationInfos presentation_infos);

  // Sends the OnFramePresented event to a particular Flatland |instance|.
  void SendFramePresented(
      FlatlandInstance* instance,
      const std::map<scheduling::PresentId, /*latched_time*/ zx::time>& latched_times,
      scheduling::PresentTimestamps present_times);

  // Removes the Flatland instance associated with |session_id|.
  void RemoveFlatlandInstance(scheduling::SessionId session_id);

  // The function passed into a Flatland constructor that allows the Flatland instance to trigger
  // its own destruction when the client makes an unrecoverable error. This function will be called
  // on Flatland instance worker threads.
  void DestroyInstanceFunction(scheduling::SessionId session_id);

  std::shared_ptr<Flatland> NewFlatland(
      std::shared_ptr<utils::DispatcherHolder> dispatcher_holder,
      fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request,
      scheduling::SessionId session_id, std::function<void()> destroy_instance_function,
      std::shared_ptr<FlatlandPresenter> flatland_presenter,
      std::shared_ptr<LinkSystem> link_system,
      std::shared_ptr<UberStructSystem::UberStructQueue> uber_struct_queue,
      const std::vector<std::shared_ptr<allocation::BufferCollectionImporter>>&
          buffer_collection_importers) const;

  // Used to assert that code is running on the expected thread.
  void CheckIsOnMainThread() const {
    FX_DCHECK(executor_.dispatcher() == async_get_default_dispatcher());
  }

  std::shared_ptr<FlatlandPresenter> flatland_presenter_;
  std::shared_ptr<UberStructSystem> uber_struct_system_;
  std::shared_ptr<LinkSystem> link_system_;
  std::vector<std::shared_ptr<allocation::BufferCollectionImporter>> buffer_collection_importers_;
  std::unordered_map<scheduling::SessionId, /*additional_present_credits*/ uint32_t>
      flatland_instances_updated_;

  // FlatlandInstances are not moveable (because fidl::Binding is not movable), therefore are reffed
  // by unique_ptr which can be moved if the map needs to do an internal reallocation.
  std::unordered_map<scheduling::SessionId, std::unique_ptr<FlatlandInstance>> flatland_instances_;

  // FlatlandDisplayInstances are not moveable (because fidl::Binding is not movable), therefore are
  // reffed by unique_ptr which can be moved if the map needs to do an internal reallocation.
  std::unordered_map<scheduling::SessionId, std::unique_ptr<FlatlandDisplayInstance>>
      flatland_display_instances_;

  // Stores and executes async tasks on the dispatcher provided in this object's constructor. This
  // object is the final member of this class to ensure that async tasks are cancelled and
  // destroyed first during destruction, else they might access already-destroyed members.
  async::Executor executor_;

  // Eventually we will support multiple displays, but as we bootstrap Flatland we assume that
  // there is a single primary display.
  std::shared_ptr<scenic_impl::display::Display> primary_display_;

  // The number of flatland instances currently alive. Used to ensure that the flatland manager gets
  // destroyed after all instances in |flatland_instances_| and |flatland_display_instances_| are
  // destroyed.
  std::atomic<uint32_t> alive_sessions_ = 0;

  // Callbacks for registering View-bound protocols.
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::Focuser>, zx_koid_t)>
      register_view_focuser_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>, zx_koid_t)>
      register_view_ref_focused_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::TouchSource>, zx_koid_t)>
      register_touch_source_;
  fit::function<void(fidl::InterfaceRequest<fuchsia::ui::pointer::MouseSource>, zx_koid_t)>
      register_mouse_source_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_MANAGER_H_
