// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>
#include <vector>

#include "lib/escher/escher/escher.h"
#include "lib/escher/escher/impl/gpu_uploader.h"
#include "lib/escher/escher/renderer/simple_image_factory.h"
#include "lib/escher/escher/resources/resource_recycler.h"
#include "lib/escher/escher/shape/rounded_rect_factory.h"
#include "lib/escher/examples/common/demo_harness.h"

#include "apps/mozart/src/scene_manager/displays/display_manager.h"
#include "apps/mozart/src/scene_manager/engine/display_swapchain.h"
#include "apps/mozart/src/scene_manager/engine/frame_scheduler.h"
#include "apps/mozart/src/scene_manager/release_fence_signaller.h"
#include "apps/mozart/src/scene_manager/resources/import.h"
#include "apps/mozart/src/scene_manager/resources/nodes/scene.h"
#include "apps/mozart/src/scene_manager/resources/resource_linker.h"
#include "apps/mozart/src/scene_manager/util/event_timestamper.h"

namespace scene_manager {

using SessionId = uint64_t;

class Compositor;
class Session;
class SessionHandler;

// Owns a group of sessions which can share resources with one another
// using the same resource linker and which coexist within the same timing
// domain using the same frame scheduler.  It is not possible for sessions
// which belong to different engines to communicate with one another.
class Engine : private FrameSchedulerDelegate {
 public:
  Engine(DisplayManager* display_manager,
         escher::Escher* escher,
         std::unique_ptr<escher::VulkanSwapchain> swapchain);

  ~Engine();

  DisplayManager* display_manager() const { return display_manager_; }
  escher::Escher* escher() const { return escher_; }
  escher::VulkanSwapchain GetVulkanSwapchain() const;

  vk::Device vk_device() {
    return escher_ ? escher_->vulkan_context().device : vk::Device();
  }

  escher::ResourceRecycler* escher_resource_recycler() {
    return escher_ ? escher_->resource_recycler() : nullptr;
  }

  escher::ImageFactory* escher_image_factory() { return image_factory_.get(); }

  escher::impl::GpuUploader* escher_gpu_uploader() {
    return escher_ ? escher_->gpu_uploader() : nullptr;
  }

  escher::RoundedRectFactory* escher_rounded_rect_factory() {
    return rounded_rect_factory_.get();
  }

  ReleaseFenceSignaller* release_fence_signaller() {
    return release_fence_signaller_.get();
  }

  ResourceLinker* resource_linker() { return &resource_linker_; }

  EventTimestamper* event_timestamper() { return &event_timestamper_; }

  // Tell the FrameScheduler to schedule a frame, and remember the Session so
  // that we can tell it to apply updates when the FrameScheduler notifies us
  // via OnPrepareFrame().
  void ScheduleSessionUpdate(uint64_t presentation_time,
                             ftl::RefPtr<Session> session);

  // Tell the FrameScheduler to schedule a frame. This is used for updates
  // triggered by something other than a Session update i.e. an ImagePipe with
  // a new Image to present.
  void ScheduleUpdate(uint64_t presentation_time);

  void CreateSession(::fidl::InterfaceRequest<scenic::Session> request,
                     ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // Create a swapchain for the specified display.  The display must not already
  // be claimed by another swapchain.
  std::unique_ptr<DisplaySwapchain> CreateDisplaySwapchain(Display* display);

  // Finds the session handler corresponding to the given id.
  SessionHandler* FindSession(SessionId id);

  size_t GetSessionCount() { return session_count_; }

  void AddCompositor(Compositor* compositor);
  void RemoveCompositor(Compositor* compositor);

 protected:
  // Only used by subclasses used in testing.
  Engine(DisplayManager* display_manager,
         std::unique_ptr<ReleaseFenceSignaller> release_fence_signaller);

 private:
  friend class SessionHandler;
  friend class Session;

  // Allow overriding to support tests.
  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // Destroys the session with the given id.
  void TearDownSession(SessionId id);

  // |FrameSchedulerDelegate|:
  void RenderFrame(const FrameTimingsPtr& frame,
                   uint64_t presentation_time,
                   uint64_t presentation_interval) override;

  // Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval);

  void InitializeFrameScheduler();

  // Update and deliver metrics for all nodes which subscribe to metrics events.
  void UpdateAndDeliverMetrics(uint64_t presentation_time);

  // Update reported metrics for nodes which subscribe to metrics events.
  // If anything changed, append the node to |updated_nodes|.
  void UpdateMetrics(Node* node,
                     const scenic::Metrics& parent_metrics,
                     std::vector<Node*>* updated_nodes);

  DisplayManager* const display_manager_;
  escher::Escher* const escher_;
  escher::PaperRendererPtr paper_renderer_;

  ResourceLinker resource_linker_;
  EventTimestamper event_timestamper_;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  std::unique_ptr<escher::RoundedRectFactory> rounded_rect_factory_;
  std::unique_ptr<ReleaseFenceSignaller> release_fence_signaller_;
  std::unique_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<escher::VulkanSwapchain> swapchain_;
  std::set<Compositor*> compositors_;

  // Map of all the sessions.
  std::unordered_map<SessionId, std::unique_ptr<SessionHandler>> sessions_;
  std::atomic<size_t> session_count_;
  SessionId next_session_id_ = 1;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::set<std::pair<uint64_t, ftl::RefPtr<Session>>> updatable_sessions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace scene_manager
