// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_VIEW_H_
#define SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_VIEW_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/base_view/base_view.h"

// Sets up an ImagePipe (including scene graph aspects) such that FrameSink can
// push frames to all the ImagePipe(s) of all the FrameSinkView(s) that are
// currently active.
//
// Registers with parent on construction and de-registers on destruction.  Only
// called on the thread that's running |loop|.
class FrameSink;
class FrameSinkView : public scenic::BaseView {
 public:
  static std::unique_ptr<FrameSinkView> Create(scenic::ViewContext context, FrameSink* parent,
                                               async::Loop* main_loop);

  ~FrameSinkView() override;

  // This is very similar to FrameSink's PutFrame, which fans out to all the
  // alive FrameSinkView(s).  This method is the leaf of that fan-out.
  void PutFrame(uint32_t image_id, zx_time_t present_time, const zx::vmo& vmo, uint64_t vmo_offset,
                const fuchsia::media::VideoUncompressedFormat& video_format, fit::closure on_done);

 private:
  FrameSinkView(scenic::ViewContext context, FrameSink* parent, async::Loop* main_loop);

  // |scenic::BaseView|
  // Called when the scene is invalidated, meaning its metrics or dimensions
  // have changed.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  FrameSink* parent_;

  async::Loop* main_loop_;

  scenic::ShapeNode node_;

  fuchsia::images::ImagePipePtr image_pipe_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameSinkView);
};

#endif  // SRC_MEDIA_LIB_TEST_INCLUDE_LIB_MEDIA_TEST_FRAME_SINK_VIEW_H_
