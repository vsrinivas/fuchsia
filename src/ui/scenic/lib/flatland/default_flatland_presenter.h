// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_

#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace flatland {

class DefaultFlatlandPresenter final : public FlatlandPresenter {
 public:
  // The |main_dispatcher| must be the dispatcher that GFX sessions run and update on. That thread
  // is typically refered to as the "main thread" or "render thread".
  explicit DefaultFlatlandPresenter(async_dispatcher_t* main_dispatcher);

  // Sets the FrameScheduler this DefaultFlatlandPresenter will use for frame scheduling calls.
  // This function should be called once before any Flatland clients begin making API calls.
  void SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);

  // |FlatlandPresenter|
  scheduling::PresentId RegisterPresent(scheduling::SessionId session_id,
                                        std::vector<zx::event> release_fences) override;

  // |FlatlandPresenter|
  void ScheduleUpdateForSession(zx::time requested_presentation_time,
                                scheduling::SchedulingIdPair id_pair) override;

 private:
  async_dispatcher_t* main_dispatcher_;
  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_
