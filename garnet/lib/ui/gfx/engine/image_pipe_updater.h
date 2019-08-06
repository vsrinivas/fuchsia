// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
#define GARNET_LIB_UI_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_

#include <lib/zx/time.h>

#include <queue>

#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"  // for CommandContext
#include "garnet/lib/ui/gfx/engine/session_context.h"

namespace scenic_impl {
namespace gfx {

class ImagePipe;
using ImagePipePtr = fxl::RefPtr<ImagePipe>;
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
class ImagePipeUpdater {
 public:
  // Return type for ApplyScheduledUpdates.
  struct ApplyScheduledUpdatesResult {
    bool needs_render;
    std::queue<PresentImageCallback> callbacks;
  };

  ImagePipeUpdater(SessionId id, std::shared_ptr<FrameScheduler> frame_scheduler);
  ~ImagePipeUpdater();

  // Called by ImagePipe::PresentImage(). Stashes the arguments without applying them; they will
  // later be applied by ApplyScheduledUpdates(). This method can also be used to clean up after an
  // ImagePipe when it is being closed/cleaned-up; in this case, pass in a null ImagePipe.
  void ScheduleImagePipeUpdate(zx::time presentation_time, const ImagePipePtr& image_pipe);

 private:
  // Make it clear that ImagePipe should only call ScheduleImagePipeUpdate(); the session is
  // responsible for deciding when to apply the updates.
  friend class Session;
  ApplyScheduledUpdatesResult ApplyScheduledUpdates(
      CommandContext* command_context, zx::time target_presentation_time,
      escher::ReleaseFenceSignaller* release_fence_signaller);

  struct ImagePipeUpdate {
    zx::time presentation_time;
    fxl::WeakPtr<ImagePipe> image_pipe;

    bool operator>(const ImagePipeUpdate& rhs) const {
      return presentation_time > rhs.presentation_time;
    }
  };
  // The least element should be on top.
  std::priority_queue<ImagePipeUpdate, std::vector<ImagePipeUpdate>, std::greater<ImagePipeUpdate>>
      scheduled_image_pipe_updates_;

  SessionId session_id_;
  std::shared_ptr<FrameScheduler> frame_scheduler_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
