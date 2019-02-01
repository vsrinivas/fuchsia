// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>
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

zx_status_t run_camera() {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  camera::Client client;
  zx_status_t status = client.Open(0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't open camera client (status " << status << ")";
    return status;
  }

  std::vector<VideoFormat> formats;
  zx_status_t driver_status;
  uint32_t total_format_count;
  uint32_t format_index = 0;
  do {
    std::vector<VideoFormat> call_formats;
    status = client.camera()->GetFormats(format_index, &formats,
                                         &total_format_count, &driver_status);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Couldn't get camera formats (status " << status << ")";
      return status;
    }

    for (auto&& f : call_formats) {
      formats.push_back(f);
    }
    format_index += call_formats.size();
  } while (formats.size() < total_format_count);

  printf("Available formats: %d\n", (int)formats.size());
  for (int i = 0; i < (int)formats.size(); i++) {
    printf("format[%d] - width: %d, height: %d, stride: %u\n", i,
           formats[i].format.width, formats[i].format.height,
           static_cast<uint32_t>(formats[i].format.planes[0].bytes_per_row));
  }

  int frame_counter = 0;
  fuchsia::camera::StreamPtr stream;

  static constexpr uint16_t kNumberOfBuffers = 8;
  fuchsia::sysmem::BufferCollectionInfo buffer_collection;
  status = Gralloc(formats[0], kNumberOfBuffers, &buffer_collection);
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
  status = client.camera()->CreateStream(std::move(buffer_collection),
                                         formats[0].rate, stream.NewRequest(),
                                         std::move(driver_token));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't set camera format. status: " << status;
    return status;
  }

  stream.events().OnFrameAvailable =
      [&stream, &loop, &frame_counter](FrameAvailableEvent frame) {
        printf("Received FrameNotify Event %d at index: %u\n", frame_counter,
               frame.buffer_id);

        if (frame.frame_status == fuchsia::camera::FrameStatus::OK) {
          stream->ReleaseFrame(frame.buffer_id);
          if (frame_counter++ > 10) {
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

  zx_status_t result = run_camera();

  return result == ZX_OK ? 0 : -1;
}
