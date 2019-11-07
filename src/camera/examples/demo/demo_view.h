// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_DEMO_VIEW_H_
#define SRC_CAMERA_EXAMPLES_DEMO_DEMO_VIEW_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <stream_provider.h>
#include <text_node.h>

#include <queue>
#include <random>

#include <src/lib/ui/base_view/base_view.h>

#include "src/camera/stream_utils/image_io_util.h"

namespace camera {

// Draws a scenic scene containing a single rectangle with an image pipe material,
// constructed with buffers populated by a stream provider.
class DemoView : public scenic::BaseView, public fuchsia::camera2::Stream_EventSender {
 public:
  explicit DemoView(scenic::ViewContext context, async::Loop* loop, bool chaos, bool image_io);
  ~DemoView() override;
  static std::unique_ptr<DemoView> Create(scenic::ViewContext context, async::Loop* loop,
                                          bool chaos, bool image_io);

 private:
  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override;

  // |fuchsia::camera2::Stream|
  void OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) override;

  void SleepIfChaos();

  async::Loop* loop_;
  bool chaos_;
  std::mt19937 chaos_gen_;
  std::binomial_distribution<uint32_t> chaos_dist_;
  fuchsia::camera2::StreamPtr stream_;
  scenic::ShapeNode node_;
  TextNode text_node_;
  fuchsia::images::ImagePipePtr image_pipe_;
  std::map<uint32_t, uint32_t> image_ids_;
  float shape_width_;
  float shape_height_;
  bool should_rotate_;
  std::queue<std::pair<std::unique_ptr<async::Wait>, zx::event>> waiters_;
  std::unique_ptr<StreamProvider> stream_provider_;

  bool image_io_;
  std::unique_ptr<camera::ImageIOUtil> image_io_util_;
};

}  // namespace camera

#endif  // SRC_CAMERA_EXAMPLES_DEMO_DEMO_VIEW_H_
