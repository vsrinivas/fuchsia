// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/factory_server.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include <fbl/unique_fd.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

namespace camera {

FactoryServer::FactoryServer()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), controller_binding_(this) {}

FactoryServer::~FactoryServer() {
  loop_.RunUntilIdle();
  controller_binding_.Unbind();
  streamer_ = nullptr;
  loop_.Quit();
  loop_.JoinThreads();
}

fit::result<std::unique_ptr<FactoryServer>, zx_status_t> FactoryServer::Create(
    fuchsia::sysmem::AllocatorHandle allocator, fuchsia::camera3::DeviceWatcherHandle watcher,
    fit::closure stop_callback) {
  auto server = std::make_unique<FactoryServer>();

  server->stop_callback_ = std::move(stop_callback);

  // Start a thread and begin processing messages.
  zx_status_t status = server->loop_.StartThread("camera-factory Loop");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return fit::error(status);
  }

  auto streamer_result =
      Streamer::Create(std::move(allocator), std::move(watcher), std::move(stop_callback));
  if (streamer_result.is_error()) {
    FX_PLOGS(ERROR, streamer_result.error()) << "Failed to create Streamer.";
    return streamer_result.take_error_result();
  }
  server->streamer_ = streamer_result.take_value();

  // Create the WebUI
  auto webui_result = WebUI::Create(server.get());
  if (webui_result.is_error()) {
    FX_PLOGS(ERROR, webui_result.error()) << "Failed to create WebUI.";
    return webui_result.take_error_result();
  }
  server->webui_ = webui_result.take_value();
  constexpr uint32_t kPortNumber = 52224;
  server->webui_->PostListen(kPortNumber);

  return fit::ok(std::move(server));
}

fidl::InterfaceRequestHandler<fuchsia::factory::camera::Controller> FactoryServer::GetHandler() {
  return fit::bind_member(this, &FactoryServer::OnNewRequest);
}

void FactoryServer::OnNewRequest(
    fidl::InterfaceRequest<fuchsia::factory::camera::Controller> request) {
  if (controller_binding_.is_bound()) {
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }

  controller_binding_.Bind(std::move(request), loop_.dispatcher());
}

void FactoryServer::Capture() {
  streamer_->RequestCapture(
      0, "", true, [&](zx_status_t status, std::unique_ptr<camera::Capture> frame) {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "capture failed";
          return;
        }
        char file[] = "/data/capture.png";
        FILE* filefp = fopen(file, "w");
        if (filefp == NULL) {
          FX_LOGS(ERROR) << "failed to open " << file << ": " << strerror(errno);
          return;
        }
        frame->WritePNGAsNV12(filefp);
        fclose(filefp);
      });
}

void FactoryServer::RequestCaptureData(uint32_t stream, CaptureResponse callback) {
  streamer_->RequestCapture(
      stream, "", true,
      [callback = callback.share()](zx_status_t status, std::unique_ptr<camera::Capture> frame) {
        callback(status, std::move(frame));
      });
}

void FactoryServer::IsIspBypassModeEnabled(bool enabled) {}

void FactoryServer::CaptureFrames(std::string dir_path, CaptureFramesCallback cb) {
  streamer_->RequestCapture(
      0, "", true, [&](zx_status_t status, std::unique_ptr<camera::Capture> frame) {
        if (status != ZX_OK) {
          FX_PLOGS(ERROR, status) << "capture failed";
          cb(ZX_OK, fuchsia::images::ImageInfo{});
          return;
        }
        if (dir_path != "") {
          if (!files::CreateDirectory("/data/" + dir_path)) {
            FX_LOGS(ERROR) << "Failed to create on-disk directory to write to.";
            return;
          }
        }
        auto file = "/data/" + dir_path + "/capture.png";
        FILE* filefp = fopen(file.data(), "w");
        if (filefp == NULL) {
          FX_LOGS(ERROR) << "failed to open " << file << ": " << strerror(errno);
          return;
        }

        // TODO(fxbug.dev/58498): Check if frame->properties_.image_format ==
        // fuchsia::sysmem::PixelFormatType::NV12
        if (bypass_) {
          frame->WritePNGUnprocessed(filefp, true);
        } else {
          frame->WritePNGAsNV12(filefp);
        }

        fclose(filefp);
        cb(ZX_OK, fuchsia::images::ImageInfo{});
      });
}

}  // namespace camera
