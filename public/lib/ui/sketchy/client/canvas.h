// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_CLIENT_CANVAS_H_
#define LIB_UI_SKETCHY_CLIENT_CANVAS_H_

#include <fuchsia/ui/sketchy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"
#include "lib/ui/sketchy/client/resources.h"

namespace sketchy_lib {

// Convenient C++ wrapper for ::fuchsia::ui::sketchy::Canvas service.
class Canvas final {
 public:
  Canvas(fuchsia::sys::StartupContext* context, async::Loop* loop);
  Canvas(::fuchsia::ui::sketchy::CanvasPtr canvas, async::Loop* loop);
  void Present(uint64_t time, scenic::Session::PresentCallback callback);

 private:
  friend class Resource;
  ResourceId AllocateResourceId();

  ::fuchsia::ui::sketchy::CanvasPtr canvas_;
  async::Loop* const loop_;
  fidl::VectorPtr<::fuchsia::ui::sketchy::Command> commands_;
  ResourceId next_resource_id_;
};

}  // namespace sketchy_lib

#endif  // LIB_UI_SKETCHY_CLIENT_CANVAS_H_
