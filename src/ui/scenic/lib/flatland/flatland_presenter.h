// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_PRESENTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_PRESENTER_H_

#include <unordered_map>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace flatland {

// Interface for Flatland instances to register user Present calls. Primarily intended to provide
// a thread-safe abstraction around a FrameScheduler.
class FlatlandPresenter {
 public:
  virtual ~FlatlandPresenter() {}

  // From scheduling::FrameScheduler::RegisterPresent():
  //
  // Registers per-present information with the frame scheduler and returns an incrementing
  // PresentId unique to that session.
  virtual scheduling::PresentId RegisterPresent(scheduling::SessionId session_id,
                                                std::vector<zx::event> release_fences) = 0;

  // From scheduling::FrameScheduler::ScheduleUpdateForSession():
  //
  // Tells the frame scheduler to schedule a frame. This is also used for updates triggered by
  // something other than a Session update i.e. an ImagePipe with a new Image to present.
  //
  // Flatland should not call this function until it has reached the acquire fences and queued an
  // UberStruct for the associated |id_pair|.
  virtual void ScheduleUpdateForSession(scheduling::SchedulingIdPair id_pair) = 0;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_FLATLAND_PRESENTER_H_
