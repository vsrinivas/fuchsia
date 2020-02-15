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

namespace scenic_impl {
namespace gfx {

namespace test {
class MockImagePipeUpdater;
}  // namespace test

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
  ImagePipeUpdater(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);
  ~ImagePipeUpdater();

  // Called by ImagePipe::PresentImage(). Stashes the arguments without applying them; they will
  // later be applied by ApplyScheduledUpdates(). This method can also be used to clean up after an
  // ImagePipe when it is being closed/cleaned-up; in this case, pass in a null ImagePipe.
  // Virtual for testing.
  virtual scheduling::PresentId ScheduleImagePipeUpdate(
      zx::time presentation_time, fxl::WeakPtr<ImagePipeBase> image_pipe,
      std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
      fuchsia::images::ImagePipe::PresentImageCallback callback);
  // |scheduling::SessionUpdater|
  UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;

  // |scheduling::SessionUpdater|
  void PrepareFrame(uint64_t trace_id) override{};

  // For tests.
  scheduling::SessionId GetSchedulingId() { return scheduling_id_; }

 private:
  friend class test::MockImagePipeUpdater;

  // Constructor for test.
  ImagePipeUpdater();

  // Map of fence listeners per present call. Listeners are removed when they are either signalled,
  // or when an UpdateSessions call for the corresponding SchedulingIdPair or a subsequent one is
  // made.
  std::map<scheduling::SchedulingIdPair, escher::FenceSetListener> fence_listeners_;
  // Map from SessionId to ImagePipe. Currently only has a single value in it, since
  // ImagePipeUpdater is mapped one to one with ImagePipes.
  std::map<scheduling::SessionId, fxl::WeakPtr<ImagePipeBase>> image_pipes_;

  const scheduling::SessionId scheduling_id_;
  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;

  fxl::WeakPtrFactory<ImagePipeUpdater> weak_factory_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_IMAGE_PIPE_UPDATER_H_
