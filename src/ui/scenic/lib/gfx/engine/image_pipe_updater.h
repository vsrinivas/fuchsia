// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_

#include <lib/zx/time.h>

#include <queue>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/present1_helper.h"

namespace scenic_impl {
namespace gfx {

namespace test {
class MockImagePipeUpdater;
}  // namespace test

class ImagePipeBase;
using PresentImageCallback = fuchsia::images::ImagePipe2::PresentImageCallback;

// ImagePipeUpdater is a helper class responsible for the scheduling and application of ImagePipe
// updates.
//
//   - ImagePipeUpdater calls FrameScheduler::ScheduleUpdateForSession() whenever a new image is
//     ready to display (i.e. all of the fences associated with the image have been signalled).
//   - FrameScheduler calls UpdateSessions() when a frame is to be rendered. At this time, the
//     most recent ready updates are applied to each ImagePipe, by calling ImagePipe::Update() on
//     the corresponding ImagePipe with the corresponding PresentId. Older scheduled updates are
//     discarded, whether their acquire fences have been signaled or not.
//   - The ImagePipe *must* call CleanupImagePipe() on destruction.
//
// Note that creating an ImagePipeUpdater does not add it to the FrameScheduler as a
// SessionUpdater; the creation code should manually do this after construction.
class ImagePipeUpdater : public scheduling::SessionUpdater,
                         public std::enable_shared_from_this<ImagePipeUpdater> {
 public:
  ImagePipeUpdater(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);
  ~ImagePipeUpdater();

  // Called in ImagePipe::PresentImage(). Waits until the |acquire_fences| for an update have been
  // reached and then schedules it with the FrameScheduler.
  // Virtual for testing.
  virtual scheduling::PresentId ScheduleImagePipeUpdate(
      scheduling::SessionId scheduling_id, zx::time presentation_time,
      fxl::WeakPtr<ImagePipeBase> image_pipe, std::vector<zx::event> acquire_fences,
      std::vector<zx::event> release_fences,
      fuchsia::images::ImagePipe2::PresentImageCallback callback);
  // |scheduling::SessionUpdater|
  UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;
  // |scheduling::SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<scheduling::SessionId, std::map<scheduling::PresentId, zx::time>>&
          latched_times,
      scheduling::PresentTimestamps present_times) override;
  // |scheduling::SessionUpdater|
  void OnCpuWorkDone() override {}

  // Removes all references to ImagePipe with |scheduling_id|.
  // Virtual for testing.
  virtual void CleanupImagePipe(scheduling::SessionId scheduling_id);

 private:
  friend class test::MockImagePipeUpdater;

  struct Pipe {
    fxl::WeakPtr<ImagePipeBase> image_pipe;
    // Handles Present1 callback semantics. Called in ScheduleImagePipeUpdate and OnFramePresented.
    scheduling::Present1Helper present1_helper;
  };

  // Constructor for test.
  ImagePipeUpdater();

  // Destroys all fence listeners for |scheduling_id| up to, but not including, |present_id|.
  void RemoveFenceListenersPriorTo(scheduling::SessionId scheduling_id,
                                   scheduling::PresentId present_id);

  // Map of fence listeners per present call. Listeners are removed when they are either signalled,
  // or when an UpdateSessions call for the corresponding SchedulingIdPair or a subsequent one is
  // made.
  std::map<scheduling::SchedulingIdPair, escher::FenceSetListener> fence_listeners_;
  // Map from SessionId to an ImagePipe and its corresponding Present1Helper.
  std::unordered_map<scheduling::SessionId, Pipe> image_pipes_;

  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
