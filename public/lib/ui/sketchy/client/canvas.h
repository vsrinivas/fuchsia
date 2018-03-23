// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SKETCHY_CLIENT_CANVAS_H_
#define LIB_UI_SKETCHY_CLIENT_CANVAS_H_

#include <fuchsia/cpp/sketchy.h>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/sketchy/client/resources.h"

namespace sketchy_lib {

// Convenient C++ wrapper for sketchy::Canvas service.
class Canvas final {
 public:
  Canvas(component::ApplicationContext* context);
  Canvas(sketchy::CanvasPtr canvas);
  void Present(uint64_t time, scenic_lib::Session::PresentCallback callback);

 private:
  friend class Resource;
  ResourceId AllocateResourceId();

  sketchy::CanvasPtr canvas_;
  fidl::VectorPtr<sketchy::Command> commands_;
  ResourceId next_resource_id_;
};

}  // namespace sketchy_lib

#endif  // LIB_UI_SKETCHY_CLIENT_CANVAS_H_
