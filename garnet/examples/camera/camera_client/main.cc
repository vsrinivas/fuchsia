// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <src/lib/fxl/logging.h>
#include <lib/zx/eventpair.h>

#include "garnet/examples/camera/camera_client/camera_client.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

using namespace fuchsia::camera;

// This is a stand-in for some actual gralloc type service which would allocate
// the right type of memory for the application and return it as a vmo.
zx_status_t Gralloc(fuchsia::camera::VideoFormat format, uint32_t num_buffers,
                    fuchsia::sysmem::BufferCollectionInfo* buffer_collection) {
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  // Simple GetBufferSize.  Only valid for simple formats:
  size_t buffer_size = ROUNDUP(
      format.format.height * format.format.planes[0].bytes_per_row, PAGE_SIZE);
  buffer_collection->buffer_count = num_buffers;
  buffer_collection->vmo_size = buffer_size;
  buffer_collection->format.set_image(std::move(format.format));
  zx_status_t status;
  for (uint32_t i = 0; i < num_buffers; ++i) {
    status = zx::vmo::create(buffer_size, 0, &buffer_collection->vmos[i]);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to allocate Buffer Collection";
      return status;
    }
  }
  return ZX_OK;
}

/*
 Note: source can either be the device index if use_camera_manager,
       or the full path to the camera driver if use_camera_manager
       is false
 */
zx_status_t run_camera(bool use_camera_manager, const char *source) {
  printf("Connecting to camera using %s\n",
         use_camera_manager ? "camera manager" : "camera driver");

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  camera::Client client;

  if (use_camera_manager) {
    client.StartManager(atoi(source));
  } else {
    client.StartDriver(source);
  }

  int frame_counter = 0;
  fuchsia::camera::StreamPtr stream;

  static constexpr uint16_t kNumberOfBuffers = 8;
  fuchsia::sysmem::BufferCollectionInfo buffer_collection;
  zx_status_t status =
      Gralloc(client.formats_[0], kNumberOfBuffers, &buffer_collection);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't allocate buffers (status " << status;
    return status;
  }

  // Create stream token.  The stream token is not very meaningful when
  // you have a direct connection to the driver, but this use case should
  // be disappearing soon anyway.  For now, we just hold on to the token.
  zx::eventpair driver_token;
  zx::eventpair stream_token;
  status = zx::eventpair::create(0, &stream_token, &driver_token);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't create driver token. status: " << status;
    return status;
  }

  if (use_camera_manager) {
    VideoStream request = {.camera_id = 0, .format = client.formats_[0]};

    status = client.manager()->CreateStream(
        request, std::move(buffer_collection), stream.NewRequest(),
        std::move(driver_token));
  } else {
    status = client.camera()->CreateStream(
        std::move(buffer_collection), client.formats_[0].rate,
        stream.NewRequest(), std::move(driver_token));
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't set camera format. status: " << status;
    return status;
  }

  stream.events().OnFrameAvailable = [&stream, &loop, &frame_counter](
                                         FrameAvailableEvent frame) {
    printf("Received FrameNotify Event %d at index: %u\n", frame_counter,
           frame.buffer_id);

    if (frame.frame_status == fuchsia::camera::FrameStatus::OK) {
      stream->ReleaseFrame(frame.buffer_id);
      if (frame_counter++ > 10) {
        FXL_LOG(INFO) << "Counted 10 frames, stopping stream and quitting loop";
        stream->Stop();
        loop.Quit();
      }
    } else {
      FXL_LOG(ERROR) << "Error set on incoming frame. Error: "
                     << static_cast<int>(frame.frame_status);
    }
  };

  stream->Start();

  printf("all done, waiting for frames...\n");

  loop.Run();

  FXL_LOG(INFO) << "Camera Test A-OK!";
  return ZX_OK;
}

int main(int argc, const char** argv) {
  printf("hello camera client\n");

  bool use_camera_manager = true;
  const char *source = "0";
  for (int i = 1; i < argc; ++i) {
    if (!strcmp("--driver", argv[i])) {
      use_camera_manager = false;
      source = "/dev/class/camera/000";
    } else if (!strcmp("--manager", argv[i])) {
      use_camera_manager = true;
      source = "0";
    } else {
      source = argv[i];
      break;
    }
  }
  printf("using source %s\n", source);

  zx_status_t result = run_camera(use_camera_manager, source);

  return result == ZX_OK ? 0 : -1;
}
