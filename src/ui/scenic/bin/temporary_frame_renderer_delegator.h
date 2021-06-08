// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_BIN_TEMPORARY_FRAME_RENDERER_DELEGATOR_H_
#define SRC_UI_SCENIC_BIN_TEMPORARY_FRAME_RENDERER_DELEGATOR_H_

#include "src/ui/scenic/lib/flatland/engine/engine.h"
#include "src/ui/scenic/lib/flatland/flatland_manager.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {

// TemporaryFrameRendererDelegator is a FrameRenderer which renders either Flatland or Gfx content,
// depending on whether a FlatlandDisplay is found or not (if so, it assumes that content is the
// only content; if not delegates to the traditional Gfx rendering path).
//
// Eventually, all content connected directly to displays will be Flatland content (any Gfx content
// will reside in sessions attached beneath Flatland sessions).  At this time, this class will
// become unnecessary.
//
// TODO(fxbug.dev/76985): this will need to be modified to support multiple displays.
class TemporaryFrameRendererDelegator : public scheduling::FrameRenderer {
 public:
  TemporaryFrameRendererDelegator(std::shared_ptr<flatland::FlatlandManager> flatland_manager,
                                  std::shared_ptr<flatland::Engine> flatland_engine,
                                  std::shared_ptr<gfx::Engine> gfx_engine);
  ~TemporaryFrameRendererDelegator() override = default;

  // |scheduling::FrameRenderer|
  void RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                            FramePresentedCallback callback) override;

  // |scheduling::FrameRenderer|
  void SignalFencesWhenPreviousRendersAreDone(std::vector<zx::event> release_fences) override;

 private:
  std::shared_ptr<flatland::FlatlandManager> flatland_manager_;
  std::shared_ptr<flatland::Engine> flatland_engine_;
  std::shared_ptr<gfx::Engine> gfx_engine_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_BIN_TEMPORARY_FRAME_RENDERER_DELEGATOR_H_
