// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_CAMERA_GYM_DISPLAY_VIEW_H_
#define SRC_CAMERA_EXAMPLES_CAMERA_GYM_DISPLAY_VIEW_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>

#include <map>

#include <src/lib/ui/base_view/base_view.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

#include "lib/ui/scenic/cpp/session.h"
#include "src/camera/examples/camera-gym/stream_provider.h"
#include "src/camera/examples/camera_display/text_node/text_node.h"

namespace camera {

// Draws a scenic scene containing a single rectangle with an image pipe material,
// constructed with buffers populated by a stream provider.
class DisplayView : public scenic::BaseView {
 public:
  explicit DisplayView(scenic::ViewContext context, async::Loop* loop);
  ~DisplayView() override;
  static std::unique_ptr<DisplayView> Create(scenic::ViewContext context, async::Loop* loop);
  zx_status_t Initialize();
  zx_status_t RunOneSession();

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override;

  // TODO(48124) - camera2 going away
  void OnFrameAvailable(uint32_t index, fuchsia::camera2::FrameAvailableInfo info);

  struct ImagePipeProperties {
    explicit ImagePipeProperties(scenic::Session* session) : node(session) {}

    // TODO(48124) - camera2 going away
    fuchsia::camera2::StreamPtr stream;

    scenic::ShapeNode node;
    fuchsia::images::ImagePipePtr image_pipe;
    std::map<uint32_t, uint32_t> image_ids;
    float shape_width;
    float shape_height;
    std::map<uint32_t, std::pair<std::unique_ptr<async::Wait>, zx::event>> waiters;
  };

  async::Loop* loop_;
  std::unique_ptr<StreamProvider> stream_provider_;
  std::vector<ImagePipeProperties> image_pipe_properties_;
  float total_width_;
  float max_height_;
  TextNode text_node_;
  trace::TraceProviderWithFdio trace_provider_;
};

}  // namespace camera

#endif  // SRC_CAMERA_EXAMPLES_CAMERA_GYM_DISPLAY_VIEW_H_
