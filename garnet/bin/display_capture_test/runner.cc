// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runner.h"

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/camera/c/fidl.h>
#include <fuchsia/hardware/display/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <zircon/device/display-controller.h>

#include <cmath>

namespace display_test {
namespace internal {

constexpr const char* kDisplayController = "/dev/class/display-controller/000";
constexpr const char* kCameraDir = "/dev/class/camera";

constexpr uint32_t kDisplayRate = 60;
constexpr uint32_t kDisplayFormat = ZX_PIXEL_FORMAT_ARGB_8888;

bool is_opaque(uint32_t val) { return (val & 0xff000000) == 0xff000000; }

// Src is always premultipled
uint32_t multiply_component(uint32_t dest, uint32_t src, uint8_t offset) {
  uint32_t alpha_comp = src >> 24;
  uint32_t dest_val =
      ((((dest >> offset) & 0xff) * (255 - alpha_comp)) + 254) / 255;
  uint32_t ret = dest_val + ((src >> offset) & 0xff);
  if (ret > 255) {
    ret = 255;
  }
  return ret << offset;
}

uint32_t multiply(uint32_t dest, uint32_t src) {
  if (is_opaque(src)) {
    return src;
  }

  return 0xff000000 | multiply_component(dest, src, 16) |
         multiply_component(dest, src, 8) | multiply_component(dest, src, 0);
}

uint8_t clip(float in) {
  return in < 0 ? 0 : (in > 255 ? 255 : static_cast<uint8_t>(in + .5));
}

// Takes 4 bytes of YUY2 and writes 8 bytes of RGBA
// TODO(stevensd): RGBA -> YUY2 might be more reliable/efficient
void yuy2_to_bgra(uint8_t* yuy2, uint32_t* argb1, uint32_t* argb2) {
  uint8_t* bgra1 = reinterpret_cast<uint8_t*>(argb1);
  uint8_t* bgra2 = reinterpret_cast<uint8_t*>(argb2);
  int u = yuy2[1] - 128;
  int y1 = yuy2[0];
  int v = yuy2[3] - 128;
  int y2 = yuy2[2];

  bgra1[0] = clip(y1 + 1.772 * u);
  bgra1[1] = clip(y1 - .344 * u - .714 * v);
  bgra1[2] = clip(y1 + 1.402 * v);
  bgra1[3] = 0xff;

  bgra2[0] = clip(y2 + 1.772 * u);
  bgra2[1] = clip(y2 - .344 * u - .714 * v);
  bgra2[2] = clip(y2 + 1.402 * v);
  bgra2[3] = 0xff;
}

static bool compare_component(uint32_t argb1, uint32_t argb2, uint32_t offset) {
  uint8_t comp1 = (argb1 >> (offset * 8)) & 0xff;
  uint8_t comp2 = (argb2 >> (offset * 8)) & 0xff;
  uint8_t diff = comp1 - comp2;
  // Unfortunately this is *very* permissive, since it's there are a lot of
  // places where slight rounding/implementation differences can accumulate
  // (i.e. the display controller blending, rgb->yuv, yuv->rgb, our blending).
  return static_cast<uint8_t>(diff + 6) < 13;
}

static bool compare_colors(uint32_t argb1, uint32_t argb2) {
  return compare_component(argb1, argb2, 0) &&
         compare_component(argb1, argb2, 1) &&
         compare_component(argb1, argb2, 2);
}

Runner::Runner(async::Loop* loop) : loop_(loop), runner_context_(this) {}

zx_pixel_format_t Runner::format() const { return kDisplayFormat; }

zx_status_t Runner::Start(const char* display_name) {
  zx_status_t status;

  display_name_ = display_name;

  camera_watcher_ = fsl::DeviceWatcher::Create(
      kCameraDir, fit::bind_member(this, &Runner::OnCameraAvailable));
  camera_stream_.events().OnFrameAvailable =
      fit::bind_member(this, &Runner::FrameNotifyCallback);

  status = loop_->Run();
  if (!display_id_ || !camera_setup_ || !display_ownership_) {
    status = ZX_ERR_INTERNAL;
  } else if (status == ZX_ERR_CANCELED) {
    status = ZX_OK;
  }
  return status;
}

void Runner::OnCameraAvailable(int dir_fd, std::string filename) {
  fbl::unique_fd fd{::openat(dir_fd, filename.c_str(), O_RDWR)};
  if (!fd.is_valid()) {
    printf("Failed to open camera %s\n", filename.c_str());
    return;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  FXL_CHECK(status == ZX_OK) << "Failed to create channel. status " << status;

  fzl::FdioCaller dev(std::move(fd));
  zx_status_t res = fuchsia_hardware_camera_DeviceGetChannel(
      dev.borrow_channel(), remote.release());
  if (res != ZX_OK) {
    printf("Failed to obtain channel for camera %s\n", filename.c_str());
    return;
  }

  camera_control_.Bind(std::move(local), loop_->dispatcher());

  camera_control_->GetFormats(
      0, fit::bind_member(this, &Runner::GetFormatCallback));
}

void Runner::GetFormatCallback(::std::vector<fuchsia::camera::VideoFormat> fmts,
                               uint32_t total_count, zx_status_t status) {
  uint32_t idx;
  for (idx = 0; idx < fmts.size(); idx++) {
    auto& format = fmts[idx];
    uint32_t capture_fps = round(1.0 * format.rate.frames_per_sec_numerator /
                                 format.rate.frames_per_sec_denominator);
    if (capture_fps != kDisplayRate) {
      continue;
    }

    if (format.format.pixel_format.type ==
        fuchsia::sysmem::PixelFormatType::YUY2) {
      break;
    }

    ZX_ASSERT(format.format.width % 2 == 0);
  }

  if (idx >= fmts.size()) {
    // Technically we should call GetFormat again, but we can handle that
    // when it comes it, since this is just a test program.
    printf("Failed to find matching capture format\n");
    return;
  }

  // We found a camera, so stop watching the dir for new cameras.
  camera_watcher_.reset();

  auto& format = fmts[idx];
  camera_stride_ = format.format.planes[0].bytes_per_row;
  width_ = format.format.width;
  height_ = format.format.height;

  fuchsia::sysmem::BufferCollectionInfo buffer_collection;
  size_t buffer_size = fbl::round_up(
      format.format.height * format.format.planes[0].bytes_per_row, 4096u);
  buffer_collection.buffer_count = kMaxFrames;
  buffer_collection.vmo_size = buffer_size;
  buffer_collection.format.set_image(std::move(format.format));

  for (uint32_t i = 0; i < kMaxFrames; ++i) {
    zx::vmo vmo;

    status = zx::vmo::create(buffer_size, 0, &vmo);
    ZX_ASSERT(status == ZX_OK);

    status = zx::vmar::root_self()->map(
        0, vmo, 0, buffer_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
        reinterpret_cast<zx_vaddr_t*>(camera_buffers_ + i));
    ZX_ASSERT(status == ZX_OK);

    buffer_collection.vmos[i] = std::move(vmo);
  }

  zx::eventpair driver_token;
  status = zx::eventpair::create(0, &stream_token_, &driver_token);
  ZX_ASSERT(status == ZX_OK);

  camera_control_->CreateStream(std::move(buffer_collection), format.rate,
                                camera_stream_.NewRequest(),
                                std::move(driver_token));

  camera_stream_->Start();
  camera_setup_ = true;

  InitDisplay();
}

void Runner::InitDisplay() {
  zx_status_t status;
  fbl::unique_fd fd(open(kDisplayController, O_RDWR));
  if (!fd.is_valid()) {
    ZX_ASSERT_MSG(false, "Failed to open display controller");
  }

  zx::channel device_server, device_client;
  status = zx::channel::create(0, &device_server, &device_client);
  if (status != ZX_OK) {
    ZX_ASSERT_MSG(false, "Failed to create device channel %d\n", status);
  }

  zx::channel dc_server, dc_client;
  status = zx::channel::create(0, &dc_server, &dc_client);
  if (status != ZX_OK) {
    ZX_ASSERT_MSG(false, "Failed to get create controller channel %d\n",
                  status);
  }

  fzl::FdioCaller dev(std::move(fd));
  zx_status_t fidl_status = fuchsia_hardware_display_ProviderOpenController(
      dev.borrow_channel(), device_server.release(), dc_server.release(),
      &status);
  if (fidl_status != ZX_OK) {
    ZX_ASSERT_MSG(false, "Failed to call service handle %d\n", status);
  }
  if (status != ZX_OK) {
    ZX_ASSERT_MSG(false, "Failed to open controller %d\n", status);
  }

  display_controller_conn_ = std::move(device_client);

  if ((status = display_controller_.Bind(std::move(dc_client),
                                         loop_->dispatcher())) != ZX_OK) {
    ZX_ASSERT_MSG(false, "Failed to bind to display controller %d\n", status);
  }
  display_controller_.events().DisplaysChanged =
      fit::bind_member(this, &Runner::OnDisplaysChanged);
  display_controller_.events().ClientOwnershipChange =
      fit::bind_member(this, &Runner::OnClientOwnershipChange);
  display_controller_.events().Vsync = fit::bind_member(this, &Runner::OnVsync);
  display_controller_.set_error_handler([this](zx_status_t status) {
    ZX_ASSERT_MSG(false, "Display controller failure");
  });

  calibration_image_a_ = runner_context_.CreateImage(width_, height_);
  calibration_image_b_ = runner_context_.CreateImage(width_, height_);
  calibration_layer_ = runner_context_.CreatePrimaryLayer(width_, height_);
  runner_context_.SetLayers(std::vector<Layer*>({calibration_layer_}));
}

void Runner::OnDisplaysChanged(
    ::std::vector<fuchsia::hardware::display::Info> added,
    ::std::vector<uint64_t> removed) {
  ZX_ASSERT_MSG(!display_id_, "Display change while tests are running");
  for (auto& info : added) {
    if (strcmp(info.monitor_name.c_str(), display_name_)) {
      continue;
    }
    for (auto& mode : info.modes) {
      if (mode.horizontal_resolution != width_ ||
          mode.vertical_resolution != height_ ||
          mode.refresh_rate_e2 != kDisplayRate * 100) {
        continue;
      }
      display_id_ = info.id;
    }
  }
  ZX_ASSERT_MSG(display_id_, "Failed to find compatible display");

  display_controller_->EnableVsync(true);

  OnResourceReady();
}

void Runner::OnClientOwnershipChange(bool is_owner) {
  if (is_owner) {
    display_ownership_ = true;
    OnResourceReady();
  } else {
    ZX_ASSERT_MSG(false, "Lost display ownership");
  }
}

void Runner::OnShutdownCallback() { ZX_ASSERT_MSG(false, "Camera shutdown"); }

void Runner::OnResourceReady() {
  if (!display_id_ || !camera_setup_ || !display_ownership_) {
    return;
  }
  if (!runner_context_.IsReady()) {
    return;
  }

  if (!test_context_) {
    loop_->Quit();
    return;
  } else if (!test_context_->IsReady()) {
    return;
  }
  test_running_ = true;

  // We know the first 2 calibration frames are fine, so skip them
  CheckFrameConfig(2);
}

void Runner::Stop() { camera_stream_->Stop(); }

Context* Runner::StartTest() {
  test_context_ = std::unique_ptr<Context>(new Context(this));

  ZX_ASSERT_MSG(!test_running_, "Test starting while busy");
  test_status_ = kTestStatusUnknown;

  calibration_layer_->SetImage(calibration_image_a_);
  runner_context_.ApplyConfig();
  calibration_layer_->SetImage(calibration_image_b_);
  runner_context_.ApplyConfig();

  return test_context_.get();
}

void Runner::FinishTest(int32_t status) {
  test_status_ = status;
  test_running_ = false;
  loop_->Quit();
}

int32_t Runner::CleanupTest() {
  ZX_ASSERT_MSG(!test_running_, "Tried to finish running test");

  test_context_.release();
  for (auto id : buffer_ids_) {
    camera_stream_->ReleaseFrame(id);
  }
  buffer_ids_.clear();
  display_idx_ = 0;
  capture_idx_ = 0;
  for (auto frame : frames_) {
    for (auto layer : frame) {
      layer.first->DeleteState(layer.second);
    }
  }
  frames_.clear();

  return test_status_;
}

void Runner::ApplyConfig(std::vector<LayerImpl*> layers) {
  std::vector<std::pair<LayerImpl*, const void*>> info;
  for (auto l : layers) {
    info.push_back(std::make_pair(l, l->ApplyState()));
  }
  frames_.push_back(std::move(info));
}

void Runner::SendFrameConfig(uint32_t frame_idx) {
  std::vector<uint64_t> layer_ids;
  auto& frame = frames_[frame_idx];
  for (auto layer : frame) {
    layer_ids.push_back(layer.first->id());
    layer.first->SendState(layer.second);
  }
  display_controller_->SetDisplayLayers(
      display_id_, fidl::VectorPtr<uint64_t>(std::move(layer_ids)));
}

void Runner::CheckFrameConfig(uint32_t frame_idx) {
  SendFrameConfig(frame_idx);
  display_controller_->CheckConfig(
      frame_idx == frames_.size() - 1,
      [this, frame_idx](
          fuchsia::hardware::display::ConfigResult result,
          ::std::vector<fuchsia::hardware::display::ClientCompositionOp>) {
        if (result == fuchsia::hardware::display::ConfigResult::OK) {
          if (frame_idx + 1 < frames_.size()) {
            CheckFrameConfig(frame_idx + 1);
          } else {
            ApplyFrame(0);
          }
        } else {
          FinishTest(kTestDisplayCheckFail);
        }
      });
}

void Runner::ApplyFrame(uint32_t frame_idx) {
  SendFrameConfig(frame_idx);
  display_controller_->ApplyConfig();
}

void Runner::OnVsync(uint64_t display_id, uint64_t timestamp,
                     ::std::vector<uint64_t> image_ids) {
  if (!test_running_ || image_ids.empty()) {
    return;
  }

  auto& frame = frames_[display_idx_];
  unsigned image_idx = 0;
  for (auto layer : frame) {
    uint64_t expected_image = layer.first->image_id(layer.second);
    if (expected_image) {
      if (image_ids[image_idx++] != expected_image) {
        if (display_idx_ != 0) {
          FinishTest(kTestVsyncFail);
        }
        return;
      }
    }
  }

  if (capture_idx_ > 0) {
    if (display_idx_ + 1 < frames_.size()) {
      ApplyFrame(++display_idx_);
    }
  }
}

void Runner::FrameNotifyCallback(
    const fuchsia::camera::FrameAvailableEvent& resp) {
  static int64_t last_timestamp = 0;
  last_timestamp = resp.metadata.timestamp;
  if (test_running_) {
    if (resp.frame_status != fuchsia::camera::FrameStatus::OK) {
      if (capture_idx_ != 0 || ++bad_capture_count_ > 5) {
        ZX_ASSERT_MSG(false, "Bad capture");
        FinishTest(kTestCaptureFail);
      }
    } else if (capture_idx_ < 2) {
      if (CheckFrame(capture_idx_, camera_buffers_[resp.buffer_id], true)) {
        capture_idx_++;
      }
    } else {
      buffer_ids_.push_back(resp.buffer_id);
      capture_idx_++;

      if (capture_idx_ == frames_.size()) {
        for (unsigned i = 2; i < frames_.size(); i++) {
          if (!CheckFrame(i, camera_buffers_[buffer_ids_[i - 2]], false)) {
            FinishTest(kTestCaptureMismatch);
            return;
          }
        }
        FinishTest(kTestOk);
      }

      // Don't release the frame, since we need to process it later.
      return;
    }
  }
  camera_stream_->ReleaseFrame(resp.buffer_id);
}

bool Runner::CheckFrame(uint32_t frame_idx, uint8_t* capture_ptr, bool quick) {
  static uint32_t quickcheck_coords[][2] = {
      {0, 0},
      {0, height_ - 1},
      {width_ - 2, 0},
      {width_ - 2, height_ - 1},
      {width_ / 2, height_ / 2},
      {UINT32_MAX, UINT32_MAX},
  };
  uint32_t quick_idx = 0;
  uint32_t x = 0;
  uint32_t y = 0;

  while (y < height_ && x < width_) {
    uint32_t expected_rgb = 0;
    bool first_layer = true;
    bool skip = false;
    for (auto layer : frames_[frame_idx]) {
      bool layer_skip;
      uint32_t layer_color;
      bool has_pixel =
          layer.first->GetPixel(layer.second, x, y, &layer_color, &layer_skip);
      if (!has_pixel) {
        continue;
      }

      if (first_layer) {
        ZX_ASSERT(is_opaque(layer_color));
        first_layer = false;
      }

      if (layer_skip) {
        skip = true;
      } else if (skip && is_opaque(layer_color)) {
        skip = false;
      }

      expected_rgb = multiply(expected_rgb, layer_color);
    }

    // There must be some fully opaque pixel
    ZX_ASSERT(!first_layer);

    if (!skip) {
      uint32_t actual_rgb1, actual_rgb2;
      // 2 bytes per pixel, so offset by x * 2
      yuy2_to_bgra(capture_ptr + (y * camera_stride_) + (x * 2), &actual_rgb1,
                   &actual_rgb2);

      if (!compare_colors(expected_rgb, actual_rgb1) ||
          !compare_colors(expected_rgb, actual_rgb2)) {
        if (!quick) {
          printf("Mismatch (%d, %d) %08x=%08x,%08x\n", x, y, expected_rgb,
                 actual_rgb1, actual_rgb2);
        }
        return false;
      }
    }

    if (quick) {
      ++quick_idx;
      x = quickcheck_coords[quick_idx][0];
      y = quickcheck_coords[quick_idx][1];
    } else {
      // Check a macropixel at a time, so += 2
      x += 2;
      if (x == width_) {
        x = 0;
        y++;
      }
    }
  }
  return true;
}

}  // namespace internal
}  // namespace display_test
