// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_VIDEO_DISPLAY_SIMPLE_CAMERA_VIEW_H_
#define SRC_CAMERA_EXAMPLES_VIDEO_DISPLAY_SIMPLE_CAMERA_VIEW_H_

#include <fuchsia/simplecamera/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <deque>
#include <list>

#include <fbl/vector.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/base_view/base_view.h"

namespace video_display {

class SimpleCameraView : public scenic::BaseView {
 public:
  explicit SimpleCameraView(scenic::ViewContext view_context);
  ~SimpleCameraView() override = default;

 private:
  // |scenic::BaseView|
  // Called when the scene is "invalidated". Invalidation happens when surface
  // dimensions or metrics change, but not necessarily when surface contents
  // change.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  scenic::ShapeNode node_;

  // Client Application:
  std::shared_ptr<sys::ServiceDirectory> simple_camera_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::simplecamera::SimpleCameraPtr simple_camera_;

  // Disallow copy and assign
  SimpleCameraView(const SimpleCameraView&) = delete;
  SimpleCameraView& operator=(const SimpleCameraView&) = delete;
};

}  // namespace video_display

#endif  // SRC_CAMERA_EXAMPLES_VIDEO_DISPLAY_SIMPLE_CAMERA_VIEW_H_
