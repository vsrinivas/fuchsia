// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/bin/temporary_frame_renderer_delegator.h"

#include <lib/syslog/cpp/macros.h>

namespace scenic_impl {

TemporaryFrameRendererDelegator::TemporaryFrameRendererDelegator(
    std::shared_ptr<flatland::FlatlandManager> flatland_manager,
    std::shared_ptr<flatland::Engine> flatland_engine, std::shared_ptr<gfx::Engine> gfx_engine)
    : flatland_manager_(std::move(flatland_manager)),
      flatland_engine_(std::move(flatland_engine)),
      gfx_engine_(std::move(gfx_engine)) {
  FX_DCHECK(flatland_manager_);
  FX_DCHECK(flatland_engine_);
  FX_DCHECK(gfx_engine_);
}

void TemporaryFrameRendererDelegator::RenderScheduledFrame(uint64_t frame_number,
                                                           zx::time presentation_time,
                                                           FramePresentedCallback callback) {
  if (auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering()) {
    flatland_engine_->RenderScheduledFrame(frame_number, presentation_time, *display.get(),
                                           std::move(callback));
  } else {
    // Render the good ol' Gfx Engine way.
    gfx_engine_->RenderScheduledFrame(frame_number, presentation_time, std::move(callback));
  }
}

void TemporaryFrameRendererDelegator::SignalFencesWhenPreviousRendersAreDone(
    std::vector<zx::event> release_fences) {
  if (auto display = flatland_manager_->GetPrimaryFlatlandDisplayForRendering()) {
    // Flatland doesn't pass release fences into the FrameScheduler.  Instead, they are stored in
    // the FlatlandPresenter and pulled out by the flatland::Engine during rendering.
    FX_CHECK(release_fences.empty()) << "Flatland fences should not be handled by FrameScheduler.";
  } else {
    // Render the good ol' Gfx Engine way.
    gfx_engine_->SignalFencesWhenPreviousRendersAreDone(std::move(release_fences));
  }
}

}  // namespace scenic_impl
