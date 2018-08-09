// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>

#include "garnet/examples/camera/camera_client/camera_client.h"

using namespace fuchsia::camera::driver;

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
    fidl::VectorPtr<VideoFormat> formats_ptr;
    status = client.camera()->GetFormats(format_index, &formats_ptr,
                                         &total_format_count, &driver_status);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Couldn't get camera formats (status " << status << ")";
      return status;
    }

    const std::vector<VideoFormat>& call_formats = formats_ptr.get();
    for (auto&& f : call_formats) {
      formats.push_back(f);
    }
    format_index += call_formats.size();
  } while (formats.size() < total_format_count);

  printf("Available formats: %d\n", (int)formats.size());
  for (int i = 0; i < (int)formats.size(); i++) {
    printf(
        "format[%d] - width: %d, height: %d, stride: %d, bits_per_pixel: %d\n",
        i, formats[i].width, formats[i].height, formats[i].stride,
        formats[i].bits_per_pixel);
  }

  int frame_counter = 0;
  StreamSyncPtr stream;
  StreamEventsPtr stream_events;
  stream_events.events().OnFrameAvailable =
      [&stream, &frame_counter](FrameAvailableEvent frame) {
        printf("Received FrameNotify Event %d at offset: %lu\n",
               frame_counter++, frame.frame_offset);

        zx_status_t driver_status;
        zx_status_t status =
            stream->ReleaseFrame(frame.frame_offset, &driver_status);
        if (frame_counter > 10) {
          status = stream->Stop(&driver_status);
        }
      };

  stream_events.events().Stopped = [&loop, &frame_counter]() {
    printf("Received Stopped Event %d\n", frame_counter++);
    if (frame_counter > 10)
      loop.Quit();
  };

  uint32_t out_max_frame_size;
  status = client.camera()->SetFormat(formats[0], stream.NewRequest(),
                                      stream_events.NewRequest(),
                                      &out_max_frame_size, &driver_status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't set camera format (status " << status << ")";
    return status;
  }

  printf("out_max_frame_size: %d\n", out_max_frame_size);

  static constexpr uint16_t kNumberOfBuffers = 8;

  FXL_LOG(INFO) << "Allocating vmo buffer of size: "
                << kNumberOfBuffers * formats[0].stride * formats[0].height;

  zx::vmo vmo_buffer;
  status = zx::vmo::create(
      kNumberOfBuffers * formats[0].stride * formats[0].height, 0, &vmo_buffer);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't create VMO buffer: " << status;
    return ZX_ERR_INTERNAL;
  }

  status = stream->SetBuffer(std::move(vmo_buffer), &driver_status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't set camera buffer (status " << status << ")";
    return status;
  }

  status = stream->Start(&driver_status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't start camera (status " << status << ")";
    return status;
  }

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
