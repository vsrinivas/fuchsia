// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/backend/framebuffer.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <magenta/device/console.h>
#include <mxio/io.h>

#include "lib/ftl/logging.h"

namespace compositor {
namespace {

constexpr char kVirtualConsole[] = "/dev/class/console/vc";

}  // namespace

std::unique_ptr<Framebuffer> Framebuffer::Open() {
  int fd = open(kVirtualConsole, O_RDWR);
  if (fd < 0) {
    FTL_DLOG(ERROR) << "Failed to open " << kVirtualConsole
                    << ": errno=" << errno;
    return nullptr;
  }

  std::unique_ptr<Framebuffer> framebuffer(new Framebuffer(fd));
  if (framebuffer->Initialize())
    return framebuffer;
  return nullptr;
}

Framebuffer::Framebuffer(int fd) : fd_(fd) {}

Framebuffer::~Framebuffer() {
  close(fd_);
}

bool Framebuffer::Initialize() {
  uint32_t full_screen = 1;
  ssize_t result = ioctl_display_set_fullscreen(fd_, &full_screen);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_DISPLAY_SET_FULLSCREEN failed: result=" << result;
    return false;
  }

  result = ioctl_console_set_active_vc(fd_);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_CONSOLE_SET_ACTIVE_VC failed: result=" << result;
    return false;
  }

  ioctl_display_get_fb_t description;
  result = ioctl_display_get_fb(fd_, &description);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_DISPLAY_GET_FB failed: result=" << result;
    return false;
  }

  vmo_.reset(description.vmo);  // we take ownership of vmo here
  info_ = description.info;
  return true;
}

bool Framebuffer::Flush() {
  ssize_t result = ioctl_display_flush_fb(fd_);
  if (result < 0) {
    FTL_DLOG(ERROR) << "IOCTL_DISPLAY_FLUSH_FB failed: result=" << result;
    return false;
  }
  return true;
}

}  // namespace compositor
