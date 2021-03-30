// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>

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
                                scheduling::SchedulingIdPair id_pair, bool squashable) override;

  // |FlatlandPresenter|
  void GetFuturePresentationInfos(scheduling::FrameScheduler::GetFuturePresentationInfosCallback
                                      presentation_infos_callback) override;

  // |FlatlandPresenter|
  void RemoveSession(scheduling::SessionId session_id) override;

 private:
  async_dispatcher_t* main_dispatcher_;
  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;

  // Ask for 8 frames of information for GetFuturePresentationInfos().
  const int64_t kDefaultPredictionInfos = 8;

  // The default frame interval assumes a 60Hz display.
  const zx::duration kDefaultFrameInterval = zx::usec(16'667);

  const zx::duration kDefaultPredictionSpan = kDefaultFrameInterval * kDefaultPredictionInfos;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_DEFAULT_FLATLAND_PRESENTER_H_
