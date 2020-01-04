// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_

#include <lib/zx/time.h>

#include <queue>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {

class ImagePipeBase;
using ImagePipeBasePtr = fxl::RefPtr<ImagePipeBase>;
using PresentImageCallback = fuchsia::images::ImagePipe::PresentImageCallback;

// ImagePipeUpdater is a helper class responsible for the scheduling and application of ImagePipe
// updates.  There is one ImagePipeUpdater per Session, which is used for all ImagePipes in the
// Session.  ImagePipeUpdater has two clients, Session and ImagePipe, who interact with it as
// follows:
//   - ImagePipe calls ScheduleImagePipeUpdate() whenever a new image is ready to display (i.e. all
//     of the fences associated with the image have been signalled).  This adds an "update" to a
//     priority queue sorted by target presentation time.
//   - Session calls ApplyScheduledUpdates() when a frame is to be rendered.  At this time, all
//     updates (from all ImagePipes in the Session) are applied, by calling ImagePipe::Update() on
//     the corresponding ImagePipe.
class ImagePipeUpdater : public scheduling::SessionUpdater {
 public:
  ImagePipeUpdater(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler,
                   escher::ReleaseFenceSignaller* release_fence_signaller);

  // Called by ImagePipe::PresentImage(). Stashes the arguments without applying them; they will
  // later be applied by ApplyScheduledUpdates(). This method can also be used to clean up after an
  // ImagePipe when it is being closed/cleaned-up; in this case, pass in a null ImagePipe.
  void ScheduleImagePipeUpdate(zx::time presentation_time, fxl::WeakPtr<ImagePipeBase> image_pipe);

  // |scheduling::SessionUpdater|
  UpdateResults UpdateSessions(const std::unordered_set<scheduling::SessionId>& sessions_to_update,
                               zx::time presentation_time, zx::time latched_time,
                               uint64_t trace_id) override;

  // |scheduling::SessionUpdater|
  void PrepareFrame(zx::time presentation_time, uint64_t trace_id) override;

  // For tests.
  scheduling::SessionId GetSchedulingId() { return scheduling_id_; }

 private:
  struct ImagePipeUpdate {
    zx::time presentation_time;
    fxl::WeakPtr<ImagePipeBase> image_pipe;

    bool operator>(const ImagePipeUpdate& rhs) const {
      return presentation_time > rhs.presentation_time;
    }
  };
  // The least element should be on top.
  std::priority_queue<ImagePipeUpdate, std::vector<ImagePipeUpdate>, std::greater<ImagePipeUpdate>>
      scheduled_image_pipe_updates_;

  const scheduling::SessionId scheduling_id_;
  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;

  // TODO(43165): Remove this pointer once the release fences are handled by FrameScheduler. This
  // code is only safe now because we guarantee that the ReleaseFenceSignaller outlives the
  // ImagePipeUpdater (ReleaseFenceSignaller lives in Engine). Do not add any additional
  // dependencies on this pointer, as this will change in the future.
  escher::ReleaseFenceSignaller* release_fence_signaller_;

  // Tracks the number of sessions returning ApplyUpdateResult::needs_render
  // and uses it for tracing.
  uint64_t needs_render_count_ = 0;
  uint64_t processed_needs_render_count_ = 0;

  fxl::WeakPtrFactory<ImagePipeUpdater> weak_factory_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
