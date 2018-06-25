// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_camera_server_app.h"

#include "lib/app/cpp/startup_context.h"

namespace simple_camera {

SimpleCameraApp::SimpleCameraApp()
    : context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {
  context_->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void SimpleCameraApp::ConnectToCamera(
    uint32_t camera_id,
    ::fidl::InterfaceHandle<::fuchsia::images::ImagePipe> image_pipe) {
  // If we fail to connect, disconnect from the client.  We only have one
  // client, so we just call CloseAll.
  zx_status_t status =
      video_display_.ConnectToCamera(camera_id, std::move(image_pipe),
                                     [this]() { this->bindings_.CloseAll(); });
  if (status != ZX_OK) {
    bindings_.CloseAll();
  }
}

}  // namespace simple_camera
