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
  printf("Not yet implemented: ConnectToCamera: %u\n", camera_id);
}

}  // namespace simple_camera
