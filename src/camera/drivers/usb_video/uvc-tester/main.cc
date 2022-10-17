// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.camera/cpp/wire.h>
#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/hardware/camera/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/align.h>

using CamControl = fuchsia::camera::ControlSyncPtr;
using Buffer = fuchsia::sysmem::BufferCollectionInfo;
using Format = fuchsia::camera::VideoFormat;

std::string ConvertPixelFormatToString(const fuchsia::sysmem::PixelFormat& format) {
  switch (format.type) {
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return "R8G8B8A8";
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return "BGRA32";
    case fuchsia::sysmem::PixelFormatType::I420:
      return "I420";
    case fuchsia::sysmem::PixelFormatType::M420:
      return "M420";
    case fuchsia::sysmem::PixelFormatType::NV12:
      return "NV12";
    case fuchsia::sysmem::PixelFormatType::YUY2:
      return "YUY2";
    case fuchsia::sysmem::PixelFormatType::MJPEG:
      return "MJPEG";
    case fuchsia::sysmem::PixelFormatType::YV12:
      return "YV12";
    case fuchsia::sysmem::PixelFormatType::BGR24:
      return "BGR24";
    case fuchsia::sysmem::PixelFormatType::RGB565:
      return "RGB565";
    case fuchsia::sysmem::PixelFormatType::RGB332:
      return "RGB332";
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      return "RGB2220";
    case fuchsia::sysmem::PixelFormatType::L8:
      return "L8";
    case fuchsia::sysmem::PixelFormatType::R8:
      return "R8";
    case fuchsia::sysmem::PixelFormatType::R8G8:
      return "R8G8";
    default:
      return "Unknown";
  }
}

std::string FormatToString(Format f) {
  uint32_t denom = f.rate.frames_per_sec_denominator;
  denom = denom > 0 ? denom : 1;
  char buffer[1024];
  sprintf(buffer, "(%u x %u) @ %u fps. stride: %u", f.format.width, f.format.height,
          f.rate.frames_per_sec_numerator / denom, f.format.planes[0].bytes_per_row);
  return ConvertPixelFormatToString(f.format.pixel_format) + buffer;
}

zx::result<CamControl> OpenCamera(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (fd.get() < 0) {
    FX_PLOGS(ERROR, fd.get()) << "Failed to open sensor at " << path;
    return zx::error(ZX_ERR_INTERNAL);
  }
  FX_LOGS(INFO) << "opened " << path;

  fuchsia::camera::ControlSyncPtr ctrl;
  fdio_cpp::UnownedFdioCaller caller(fd);
  auto status = fidl::WireCall(caller.borrow_as<fuchsia_hardware_camera::Device>())
                    ->GetChannel(ctrl.NewRequest().TakeChannel())
                    .status();
  if (status != ZX_OK) {
    FX_PLOGS(INFO, status) << "Couldn't GetChannel from " << path;
    return zx::error(status);
  }

  fuchsia::camera::DeviceInfo info_return;
  status = ctrl->GetDeviceInfo(&info_return);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not get Device Info";
    return zx::error(status);
  }
  FX_LOGS(INFO) << "Got Device Info:\n"
                << "  Vendor: " << info_return.vendor_name << " (" << info_return.vendor_id << ")";
  return zx::ok(std::move(ctrl));
}

zx::result<std::vector<fuchsia::camera::VideoFormat>> GetFormats(CamControl& ctrl) {
  std::vector<fuchsia::camera::VideoFormat> all_formats;
  uint32_t total_format_count;
  uint32_t format_index = 0;
  do {
    std::vector<fuchsia::camera::VideoFormat> call_formats;

    zx_status_t out_status;
    auto status = ctrl->GetFormats(format_index, &call_formats, &total_format_count, &out_status);
    if (out_status != ZX_OK) {
      FX_LOGS(ERROR) << "Couldn't get camera formats ( out_status " << out_status << ")";
      return zx::error(out_status);
    }
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Couldn't get camera formats (status " << status << ")";
      return zx::error(status);
    }

    for (auto&& f : call_formats) {
      all_formats.push_back(f);
    }
    format_index += call_formats.size();
  } while (all_formats.size() < total_format_count);

  FX_LOGS(INFO) << "Found " << all_formats.size() << " formats.";
  for (size_t i = 0; i < all_formats.size(); i++) {
    uint32_t denom = all_formats[i].rate.frames_per_sec_denominator;
    denom = denom > 0 ? denom : 1;
    FX_LOGS(INFO) << "Format " << i << " - " << FormatToString(all_formats[i]);
  }
  return zx::ok(all_formats);
}

zx::result<Buffer> Gralloc(fuchsia::camera::VideoFormat format, uint32_t num_buffers = 8) {
  Buffer buffer_collection;
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  // Simple GetBufferSize.  Only valid for simple formats:
  size_t buffer_size =
      ZX_ROUNDUP(format.format.height * format.format.planes[0].bytes_per_row, PAGE_SIZE);
  buffer_collection.buffer_count = num_buffers;
  buffer_collection.vmo_size = buffer_size;
  buffer_collection.format.image = format.format;
  zx_status_t status;
  for (uint32_t i = 0; i < num_buffers; ++i) {
    status = zx::vmo::create(buffer_size, 0, &buffer_collection.vmos[i]);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to allocate Buffer Collection";
      return zx::error(status);
    }
  }
  return zx::ok(std::move(buffer_collection));
}

zx_status_t StreamFrames(CamControl& ctrl, Format format, async_dispatcher_t* dispatcher,
                         int num_frames = 10) {
  FX_LOGS(INFO) << "Attempting to stream " << num_frames
                << " frames with Format: " << FormatToString(format);
  fuchsia::camera::StreamPtr stream;
  int frame_counter = 0;
  auto buffer_or = Gralloc(format);
  if (buffer_or.is_error()) {
    return buffer_or.error_value();
  }
  zx::eventpair driver_token, stream_token;
  auto status = zx::eventpair::create(0, &stream_token, &driver_token);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't create driver token. status: " << status;
    return status;
  }
  stream.events().OnFrameAvailable = [&stream, &num_frames,
                                      &frame_counter](fuchsia::camera::FrameAvailableEvent frame) {
    FX_LOGS(DEBUG) << "Received FrameNotify Event " << frame_counter
                   << " at index: " << frame.buffer_id;
    if (frame.frame_status != fuchsia::camera::FrameStatus::OK) {
      FX_LOGS(ERROR) << "Error set on incoming frame. Error: "
                     << static_cast<int>(frame.frame_status);
      return;
    }

    if (frame_counter++ > num_frames) {
      FX_LOGS(INFO) << "Counted " << frame_counter << " frames, stopping stream and quitting loop";
      stream->Stop();
      // Close the channel, to check if the eventpair gets closed:
      stream.Unbind().TakeChannel().reset();
    } else {
      stream->ReleaseFrame(frame.buffer_id);
    }
  };

  status = ctrl->CreateStream(std::move(*buffer_or), format.rate, stream.NewRequest(dispatcher),
                              std::move(driver_token));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Couldn't create Stream";
    return status;
  }
  stream->Start();
  // Streaming num_frames should take (num_frames / fps) seconds, mult. by 1000 for msec.
  uint32_t denom = format.rate.frames_per_sec_denominator;
  denom = denom > 0 ? denom : 1;
  int64_t estimated_time_ms = (num_frames * static_cast<int64_t>(denom) * 1000);
  estimated_time_ms /= format.rate.frames_per_sec_numerator;
  estimated_time_ms += 5000;  // give an extra 5 seconds for any setup and teardown.
  FX_LOGS(INFO) << "Waiting " << estimated_time_ms << " ms for stream to deliver frames"
                << " and shut down.";
  zx_signals_t observed = 0;
  stream_token.wait_one(ZX_EVENTPAIR_SIGNALED | ZX_EVENTPAIR_PEER_CLOSED,
                        zx::deadline_after(zx::msec(estimated_time_ms)), &observed);
  if (!observed) {
    FX_LOGS(ERROR) << "Timed out after seeing " << frame_counter << " frames.";
    return ZX_ERR_INTERNAL;
  }
  FX_LOGS(INFO) << "Stream closed properly after observing " << frame_counter << " frames.";

  return ZX_OK;
}

int main(int argc, const char** argv) {
  // Create the main async event loop.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread();

  auto result = OpenCamera("/dev/class/camera/000");
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error_value()) << "Unable to open camera.";
    return -1;
  }

  auto formats_or = GetFormats(*result);
  if (formats_or.is_error()) {
    FX_PLOGS(ERROR, formats_or.error_value()) << "Unable to get formats.";
    return -1;
  }

  for (auto format : formats_or.value()) {
    auto status = StreamFrames(*result, format, loop.dispatcher());
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to stream frames.";
      return -1;
    }
  }
  FX_LOGS(INFO) << "Streamed all formats successfully.";
  return 0;
}
