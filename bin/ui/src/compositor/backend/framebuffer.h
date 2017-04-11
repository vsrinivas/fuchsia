// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_FRAMEBUFFER_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_FRAMEBUFFER_H_

#include <magenta/device/display.h>
#include <magenta/pixelformat.h>
#include <mx/vmo.h>

#include <memory>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"

namespace compositor {

namespace {

enum FramebufferType { kVirtualConsole, kDisplay };

}  // namespace

class Framebuffer {
 public:
  static std::unique_ptr<Framebuffer> OpenFromDisplay();
  static std::unique_ptr<Framebuffer> OpenFromVirtualConsole();

  ~Framebuffer();

  const mx::vmo& vmo() const { return vmo_; }
  const mx_display_info_t& info() const { return info_; }

  bool Flush();

 private:
  static std::unique_ptr<Framebuffer> Open(FramebufferType type);

  explicit Framebuffer(ftl::UniqueFD fd, FramebufferType framebuffer_type);
  bool Initialize();

  ftl::UniqueFD fd_;
  mx::vmo vmo_;
  mx_display_info_t info_ = {};
  FramebufferType type_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Framebuffer);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_FRAMEBUFFER_H_
