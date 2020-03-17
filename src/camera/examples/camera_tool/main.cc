// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/bridge.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>

#include "src/camera/examples/camera_tool/buffer_collage.h"
#include "src/camera/lib/virtual_camera/virtual_camera.h"

int main(int argc, char* argv[]) {
  syslog::InitLogger({"camera"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  // Connect to required services.
  fuchsia::sysmem::AllocatorHandle allocator;
  zx_status_t status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return 0;
  }
  fuchsia::ui::policy::PresenterHandle presenter;
  status = context->svc()->Connect(presenter.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Presenter service.";
    return 0;
  }
  fuchsia::ui::scenic::ScenicHandle scenic;
  status = context->svc()->Connect(scenic.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Scenic service.";
    return 0;
  }

  // Create the collage.
  auto collage_result = camera::BufferCollage::Create(
      std::move(presenter), std::move(scenic), std::move(allocator), [&] {
        FX_LOGS(INFO) << "BufferCollage stopped. Quitting loop.";
        loop.Quit();
      });
  if (collage_result.is_error()) {
    FX_PLOGS(ERROR, collage_result.error()) << "Failed to create BufferCollage.";
    return 0;
  }
  auto collage = collage_result.take_value();

  // Create a virtual camera to drive the collage.
  // TODO: replace with DeviceWatcher and real camera content.
  status = context->svc()->Connect(allocator.NewRequest());
  auto camera_result = camera::VirtualCamera::Create(std::move(allocator));
  if (camera_result.is_error()) {
    FX_PLOGS(ERROR, camera_result.error()) << "Failed to create VirtualCamera.";
    return 0;
  }
  auto camera = camera_result.take_value();

  auto MakeError = [&](std::string name) {
    return [&, name](zx_status_t status) {
      FX_PLOGS(ERROR, status) << name << " disconnected unexpectedly.";
      loop.Quit();
    };
  };
  fuchsia::camera3::DevicePtr device;
  device.set_error_handler(MakeError("Camera"));
  camera->GetHandler()(device.NewRequest());

  fuchsia::camera3::StreamPtr stream;
  stream.set_error_handler(MakeError("Stream"));
  device->ConnectToStream(0, stream.NewRequest());

  fuchsia::sysmem::AllocatorPtr allocator_ptr;
  allocator_ptr.set_error_handler(MakeError("Allocator"));
  status = context->svc()->Connect(allocator_ptr.NewRequest());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to request Allocator service.";
    return 0;
  }

  fit::bridge<fuchsia::sysmem::ImageFormat_2> bridge;
  device->GetConfigurations([&](std::vector<fuchsia::camera3::Configuration> configurations) {
    ZX_ASSERT(!configurations.empty());
    ZX_ASSERT(!configurations[0].streams.empty());
    bridge.completer.complete_ok(configurations[0].streams[0].image_format);
  });

  fuchsia::sysmem::BufferCollectionTokenHandle token;
  allocator_ptr->AllocateSharedCollection(token.NewRequest());
  stream->SetBufferCollection(std::move(token));
  uint32_t collection_id = 0;
  fit::function<void(fuchsia::camera3::FrameInfo)> frame_handler =
      [&](fuchsia::camera3::FrameInfo info) {
        collage->ShowBuffer(collection_id, info.buffer_index, std::move(info.release_fence),
                            std::nullopt);
        stream->GetNextFrame(frame_handler.share());
      };
  stream->WatchBufferCollection([&](fuchsia::sysmem::BufferCollectionTokenHandle token) {
    auto promise = bridge.consumer.promise()
                       .and_then([&](fuchsia::sysmem::ImageFormat_2& image_format) {
                         return collage->AddCollection(std::move(token), image_format,
                                                       "Virtual Camera Stream 0:0");
                       })
                       .and_then([&](uint32_t& returned_collection_id) {
                         collection_id = returned_collection_id;
                         stream->GetNextFrame(frame_handler.share());
                       })
                       .or_else([&] {
                         FX_LOGS(ERROR) << "Failed to add collection";
                         loop.Quit();
                       });
    fit::run_single_threaded(std::move(promise));
  });

  // Run the loop.
  status = loop.Run();
  if (status == ZX_ERR_CANCELED) {
    FX_LOGS(INFO) << "Main loop terminated normally.";
  } else {
    FX_LOGS(WARNING) << "Main loop terminated abnormally.";
  }
  return 0;
}
