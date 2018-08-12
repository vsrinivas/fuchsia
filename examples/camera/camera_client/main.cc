// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>

#include "garnet/examples/camera/camera_client/camera_client.h"

using namespace fuchsia::camera::driver;

// This is a stand-in for some actual gralloc type service which would allocate
// the right type of memory for the application and return it as a vmo.
zx_status_t Gralloc(fuchsia::camera::driver::VideoFormat format,
                    uint32_t num_buffers,
                    fuchsia::sysmem::BufferCollectionInfo* buffer_collection) {
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  // Simple GetBufferSize.  Only valid for simple formats:
  size_t buffer_size = format.format.height * format.format.bytes_per_row;
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
        "format[%d] - width: %d, height: %d, stride: %lu\n",
        i, formats[i].format.width, formats[i].format.height,
        formats[i].format.bytes_per_row);
  }

  int frame_counter = 0;
  StreamSyncPtr stream;
  StreamEventsPtr stream_events;
  stream_events.events().OnFrameAvailable =
      [&stream, &frame_counter](FrameAvailableEvent frame) {
        printf("Received FrameNotify Event %d at index: %u\n",
               frame_counter++, frame.buffer_id);

        zx_status_t driver_status;
        zx_status_t status =
            stream->ReleaseFrame(frame.buffer_id, &driver_status);
        if (frame_counter > 10) {
          status = stream->Stop(&driver_status);
        }
      };

  static constexpr uint16_t kNumberOfBuffers = 8;
  fuchsia::sysmem::BufferCollectionInfo buffer_collection;
  status = Gralloc(formats[0], kNumberOfBuffers, &buffer_collection);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't allocate buffers (status " << status;
    return status;
  }

  stream_events.events().Stopped = [&loop, &frame_counter]() {
    printf("Received Stopped Event %d\n", frame_counter++);
    if (frame_counter > 10)
      loop.Quit();
  };

  status = client.camera()->CreateStream(
      std::move(buffer_collection), formats[0].rate,
      stream.NewRequest(), stream_events.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't set camera format (status " << status << ")";
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
