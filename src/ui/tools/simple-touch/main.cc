// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/input/report/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/framebuffer/framebuffer.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/device.h>

namespace simple_touch {

namespace fidl_report = ::fuchsia::input::report;

typedef struct display_info {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  zx_pixel_format_t format;
} display_info_t;

// This class manages the framebuffer. It will initialize the buffer, draw to it,
// and flush it back to memory.
// At the moment we only support a single buffer with a pixel size of 32 byes and
// a color format of RGBA.
class FrameBuffer {
 public:
  ~FrameBuffer();
  zx_status_t Init();
  // Draw a square point centered at |x| and |y| with |width| and |height|.
  void DrawPoint(uint32_t color, uint32_t x, uint32_t y, uint8_t width, uint8_t height);
  void FlushScreen() { zx_cache_flush(pixels_, pixels_size_, ZX_CACHE_FLUSH_DATA); }
  void ClearScreen();
  display_info_t DisplayInfo() { return display_info_; }

 private:
  display_info_t display_info_ = {};
  uint32_t* pixels_ = nullptr;
  size_t pixels_size_ = 0;
};

FrameBuffer::~FrameBuffer() {
  if (pixels_) {
    _zx_vmar_unmap(zx_vmar_root_self(), reinterpret_cast<zx_vaddr_t>(pixels_), pixels_size_);
  }
  fb_release();
}

zx_status_t FrameBuffer::Init() {
  const char* err;
  zx_status_t status = fb_bind(true, &err);
  if (status != ZX_OK) {
    printf("failed to open framebuffer: %d (%s)\n", status, err);
    return status;
  }
  display_info_t info;
  fb_get_config(&info.width, &info.height, &info.stride, &info.format);

  zx_handle_t vmo = fb_get_single_buffer();

  printf("format = %d\n", info.format);
  printf("width = %d\n", info.width);
  printf("height = %d\n", info.height);
  printf("stride = %d\n", info.stride);

  pixels_size_ = info.stride * ZX_PIXEL_FORMAT_BYTES(info.format) * info.height;
  uintptr_t frame_buffer_ptr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0,
                       pixels_size_, &frame_buffer_ptr);
  if (status != ZX_OK) {
    return status;
  }

  pixels_ = (uint32_t*)frame_buffer_ptr;
  display_info_ = info;

  ClearScreen();
  FlushScreen();
  return ZX_OK;
}

void FrameBuffer::DrawPoint(uint32_t color, uint32_t x, uint32_t y, uint8_t width, uint8_t height) {
  uint32_t fb_width = display_info_.stride;
  uint32_t fb_height = display_info_.height;
  uint32_t xrad = (width + 1) / 2;
  uint32_t yrad = (height + 1) / 2;

  uint32_t xmin = (xrad > x) ? 0 : x - xrad;
  uint32_t xmax = (xrad > fb_width - x) ? fb_width : x + xrad;
  uint32_t ymin = (yrad > y) ? 0 : y - yrad;
  uint32_t ymax = (yrad > fb_height - y) ? fb_height : y + yrad;

  for (uint32_t px = xmin; px < xmax; px++) {
    for (uint32_t py = ymin; py < ymax; py++) {
      *(pixels_ + py * fb_width + px) = color;
    }
  }
}

void FrameBuffer::ClearScreen() { memset(pixels_, 0xff, pixels_size_); }

// This class sits over the framebuffer and is responsible for associating touches with color,
// for drawing the clear and exit button, and for recognizing button touches.
class TouchApp {
 public:
  zx_status_t Init() {
    zx_status_t status = frame_buffer_.Init();
    if (status != ZX_OK) {
      return status;
    }
    display_info_ = frame_buffer_.DisplayInfo();

    ClearScreen();
    FlushScreen();

    status = GetTouchScreen();
    if (status != ZX_OK) {
      return status;
    }

    return ZX_OK;
  }

  void ClearScreen() {
    frame_buffer_.ClearScreen();
    frame_buffer_.DrawPoint(0xff00ff, display_info_.stride - (kButtonSize / 2), (kButtonSize / 2),
                            kButtonSize, kButtonSize);
    frame_buffer_.DrawPoint(0x0000ff, (kButtonSize / 2), display_info_.height - (kButtonSize / 2),
                            kButtonSize, kButtonSize);
  }

  void FlushScreen() { frame_buffer_.FlushScreen(); }

  void DrawPoint(uint32_t color, uint32_t x, uint32_t y, uint8_t width, uint8_t height) {
    x = x * display_info_.width / max_x_;
    y = y * display_info_.height / max_y_;
    frame_buffer_.DrawPoint(color, x, y, width, height);

    if (x + kButtonSize > display_info_.width && y < kButtonSize) {
      ClearScreen();
      FlushScreen();
    }
    if (((y + kButtonSize) > display_info_.height) && (x < kButtonSize)) {
      run_ = false;
    }
  }

  void SetMaxValues(uint32_t x, uint32_t y) {
    max_x_ = x;
    max_y_ = y;
  }

  std::vector<fidl_report::InputReport> reports;
  int Run() {
    zx_status_t status;
    run_ = true;
    while (run_) {
      // Wait on the event to be readable.
      status = has_reports_event_.wait_one(DEV_STATE_READABLE, zx::time::infinite(), nullptr);
      if (status != ZX_OK) {
        return 1;
      }

      // Get the report.
      status = client_->GetReports(&reports);
      if (status != ZX_OK) {
        printf("GetReports FIDL call returned %s\n", zx_status_get_string(status));
        return 1;
      }

      for (auto& report : reports) {
        if (!report.has_touch()) {
          continue;
        }
        if (!report.touch().has_contacts()) {
          continue;
        }
        for (size_t i = 0; i < report.touch().contacts().size(); i++) {
          uint32_t x = report.touch().contacts()[i].position_x();
          uint32_t y = report.touch().contacts()[i].position_y();
          uint32_t contact_id = report.touch().contacts()[i].contact_id();
          uint32_t width = 10;
          uint32_t height = 10;
          DrawPoint(kColors[contact_id % kColors.size()], x, y, width, height);
        }
      }
      FlushScreen();
    }
    return 0;
  }

 private:
  static constexpr uint32_t kButtonSize = 50;
  // Array of colors for each finger
  static constexpr std::array<uint32_t, 10> kColors = {
      0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00, 0x00ff00ff,
      0x0000ffff, 0x00000000, 0x00f0f0f0, 0x00f00f00, 0x000ff000,
  };

  // Gets the touch client from a file path. Sets |client_| on success.
  zx_status_t GetClientFromFilePath(const char* path) {
    fbl::unique_fd fd(open(path, O_RDWR));
    if (!fd.is_valid()) {
      return ZX_ERR_INTERNAL;
    }

    zx::channel chan;
    zx_status_t status = fdio_get_service_handle(fd.release(), chan.reset_and_get_address());
    if (status != ZX_OK) {
      printf("Ftdio get handle failed with %s\n", zx_status_get_string(status));
      return status;
    }
    client_.Bind(std::move(chan));
    return ZX_OK;
  }

  bool IsTouchscreen(const fidl_report::DeviceDescriptor& descriptor) {
    if (!descriptor.has_touch() || !descriptor.touch().has_input()) {
      return false;
    }
    const fidl_report::TouchInputDescriptor& touch_desc = descriptor.touch().input();
    if (!touch_desc.has_touch_type() ||
        (touch_desc.touch_type() != fidl_report::TouchType::TOUCHSCREEN)) {
      return false;
    }
    return true;
  }

  // Iterates through the input-report directory and finds a touchscreen.
  // Gets that touchscreen's client and report event.
  zx_status_t GetTouchScreen() {
    // Find the touchscreen.
    struct dirent* de;
    DIR* dir = opendir("/dev/class/input-report");
    if (!dir) {
      printf("failed to open %s: %d\n", "/dev/class/input-report", errno);
      return ZX_ERR_INTERNAL;
    }

    while ((de = readdir(dir)) != NULL) {
      char devname[128];

      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
        continue;
      }

      // Get the |client_| from the path.
      snprintf(devname, sizeof(devname), "%s/%s", "/dev/class/input-report", de->d_name);
      zx_status_t status = GetClientFromFilePath(devname);

      // Get the DeviceDescriptor.
      fidl_report::DeviceDescriptor device_descriptor;
      status = client_->GetDescriptor(&device_descriptor);
      if (status != ZX_OK) {
        printf("GetDescriptor FIDL call returned %s\n", zx_status_get_string(status));
        return status;
      }

      if (!IsTouchscreen(device_descriptor)) {
        continue;
      }

      SetMaxValues(device_descriptor.touch().input().contacts()[0].position_x().range.max,
                   device_descriptor.touch().input().contacts()[0].position_y().range.max);

      printf("Found touchscreen at %s\n", devname);

      // Get the reports event.
      zx_status_t wire_status = client_->GetReportsEvent(&status, &has_reports_event_);
      if (wire_status != ZX_OK) {
        printf("GetReportsEvent FIDL call returned %s\n", zx_status_get_string(wire_status));
        return 1;
      }
      if (status != ZX_OK) {
        printf("GetReportsEvent FIDL call returned %s\n", zx_status_get_string(status));
        return 1;
      }
      return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
  }

  uint32_t max_x_ = 0;
  uint32_t max_y_ = 0;
  zx::event has_reports_event_;
  fidl_report::InputDeviceSyncPtr client_;
  FrameBuffer frame_buffer_;
  display_info_t display_info_ = {};
  bool run_ = true;
};

}  // namespace simple_touch

int main(int argc, char* argv[]) {
  simple_touch::TouchApp app;
  zx_status_t status = app.Init();
  if (status != ZX_OK) {
    return 1;
  }

  return app.Run();
}
