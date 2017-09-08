// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/canvas.h"

namespace sketchy_lib {

Canvas::Canvas(app::ApplicationContext* context)
    : Canvas(context->ConnectToEnvironmentService<sketchy::Canvas>()) {}

Canvas::Canvas(sketchy::CanvasPtr canvas)
    : canvas_(std::move(canvas)), next_resource_id_(1) {
  canvas_.set_connection_error_handler([this] {
    FTL_LOG(INFO) << "sketchy_lib::Canvas: lost connection to sketchy::Canvas.";
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });
}

ResourceId Canvas::AllocateResourceId() {
  return next_resource_id_++;
}

void Canvas::Present(uint64_t time) {
  if (!ops_.empty()) {
    canvas_->Enqueue(std::move(ops_));
  }
  // TODO: Use this callback to drive Present loop.
  canvas_->Present(time, [](scenic::PresentationInfoPtr info) {});
}

}  // namespace sketchy_lib
