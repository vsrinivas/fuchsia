// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer.h"

#include <fcntl.h>
#include <stdio.h>

#include <magenta/device/console.h>
#include <magenta/device/display.h>
#include <mxio/io.h>

#include "lib/ftl/logging.h"

namespace compositor {
namespace {

constexpr char kDisplayPath[] = "/dev/class/display/000";
constexpr char kVirtualConsolePath[] = "/dev/class/console/vc";

}  // namespace

std::unique_ptr<Framebuffer> Framebuffer::OpenFromDisplay() {
  return Framebuffer::Open(FramebufferType::kDisplay);
}

std::unique_ptr<Framebuffer> Framebuffer::OpenFromVirtualConsole() {
  return Framebuffer::Open(FramebufferType::kVirtualConsole);
}

std::unique_ptr<Framebuffer> Framebuffer::Open(FramebufferType type) {
  auto get_device_path = [](FramebufferType type) {
    switch (type) {
      case kDisplay:
        return kDisplayPath;
      case kVirtualConsole:
        return kVirtualConsolePath;
    }
  };
  const char* device = get_device_path(type);

  int result = open(device, O_RDWR);
  if (result < 0) {
    FTL_DLOG(ERROR) << "Failed to open " << device << ": errno=" << errno;
    return nullptr;
  }

  std::unique_ptr<Framebuffer> framebuffer(
      new Framebuffer(ftl::UniqueFD(result), type));
  if (framebuffer->Initialize())
    return framebuffer;
  return nullptr;
}

Framebuffer::Framebuffer(ftl::UniqueFD fd, FramebufferType type)
    : fd_(std::move(fd)), type_(type) {}

Framebuffer::~Framebuffer() {}

bool Framebuffer::Initialize() {
  uint32_t full_screen = 1;
  ssize_t result;
  if (type_ == FramebufferType::kVirtualConsole) {
    result = ioctl_display_set_fullscreen(fd_.get(), &full_screen);
    if (result < 0) {
      FTL_DLOG(ERROR) << "IOCTL_DISPLAY_SET_FULLSCREEN failed: result="
                      << result;
      return false;
    }

    result = ioctl_console_set_active_vc(fd_.get());
    if (result < 0) {
      FTL_DLOG(ERROR) << "IOCTL_CONSOLE_SET_ACTIVE_VC failed: result="
                      << result;
      return false;
    }
  }

  ioctl_display_get_fb_t description;
  result = ioctl_display_get_fb(fd_.get(), &description);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    return false;
  }

  vmo_.reset(description.vmo);  // We take ownership of the VMO here.
  info_ = description.info;
  return true;
}

bool Framebuffer::Flush() {
  // Don't need to flush the display driver.
  if (type_ == FramebufferType::kVirtualConsole) {
    ssize_t result = ioctl_display_flush_fb(fd_.get());
    if (result < 0) {
      FTL_DLOG(ERROR) << "IOCTL_DISPLAY_FLUSH_FB failed: result=" << result;
      return false;
    }
  }
  return true;
}

}  // namespace compositor
