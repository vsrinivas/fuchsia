// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <unordered_set>

#include "escher/vk/vulkan_swapchain.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "ftl/macros.h"

namespace ftl {
class TaskRunner;
}  // namespace ftl

namespace mozart {
namespace scene {

class Display;
class Renderer;

// Interface implemented by anyone who needs to know when the FrameScheduler is
// preparing to initiate the rendering of a frame.
class FrameSchedulerListener {
 public:
  // Return true if the listener has knowledge that the scene is dirty, and
  // must be redrawn.  If no listener returns true, the FrameScheduler has the
  // option of not drawing a frame.
  virtual bool OnPrepareFrame(uint64_t presentation_time,
                              uint64_t presentation_interval) = 0;
};

// The FrameScheduler is responsible for scheduling frames to be drawn in
// response to requests from clients.  When a frame is requested, the
// FrameScheduler will decide at which Vsync the frame should be displayed at.
// This time will be no earlier than the requested time, and will be as close
// as possible to the requested time, subject to various constraints.  For
// example, if the requested time is earlier than the time that rendering would
// finish, were it started immediately, then the frame will be scheduled for a
// later Vsync.
class FrameScheduler {
 public:
  FrameScheduler(escher::Escher* escher,
                 escher::VulkanSwapchain swapchain,
                 Display* display);
  ~FrameScheduler();

  // Request a frame to be scheduled at or after |presentation_time|, which
  // may be in the past.
  void RequestFrame(uint64_t presentation_time);

  void AddRenderer(Renderer* renderer);
  void RemoveRenderer(Renderer* renderer);

  void AddListener(FrameSchedulerListener* listener);
  void RemoveListener(FrameSchedulerListener* listener);

  // Return a time > last_presentation_time_ if a frame should be scheduled.
  // Otherwise, return last_presentation_time_ to indicate that no frame needs
  // to be scheduled.
  uint64_t ComputeTargetPresentationTime(uint64_t now) const;

 private:
  // Update the global scene and then draw it... maybe.  There are multiple
  // reasons why this might not happen.  For example, the swapchain might apply
  // back-pressure if we can't hit our target frame rate.  Or, after this frame
  // was scheduled, another frame was scheduled to be rendered at an earlier
  // time, and not enough time has elapsed to render this frame.  Etc.
  void MaybeUpdateSceneAndDrawFrame();

  // Helper function that posts a task if there are pending presentation
  // requests.
  void MaybeScheduleFrame();

  // Called before DrawFrame() to update the global scene-graph, by notifying
  // all listeners that a frame is about to be rendered/presented.
  void UpdateScene();

  // Called after UpdateScene() in order to render the global scene-graph.
  void DrawFrame();

  // Returns true to apply back-pressure when we cannot hit our target frame
  // rate.  Otherwise, return false to indicate that it is OK to immediately
  // render a frame.
  bool TooMuchBackPressure();

  ftl::TaskRunner* const task_runner_;
  escher::VulkanSwapchainHelper swapchain_helper_;
  std::unordered_set<Renderer*> renderers_;
  // TODO: apply listeners in order.  For example, this would allow clients of
  // FrameScheduler to apply session updates before updating animations.
  std::unordered_set<FrameSchedulerListener*> listeners_;

  uint64_t last_presentation_time_ = 0;
  uint64_t next_presentation_time_ = 0;
  std::priority_queue<uint64_t> requested_presentation_times_;

  Display* const display_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FrameScheduler);
};

}  // namespace scene
}  // namespace mozart
