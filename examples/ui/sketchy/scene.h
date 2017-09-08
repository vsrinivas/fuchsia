// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"
#include "lib/ui/sketchy/canvas.h"

namespace sketchy_example {

class Scene {
 public:
  Scene(scenic_lib::Session* session, float width, float height);

  scenic_lib::EntityNode& stroke_group_holder() { return stroke_group_holder_; }

 private:
  scenic_lib::DisplayCompositor compositor_;
  scenic_lib::EntityNode stroke_group_holder_;
};

}  // namespace sketchy_example
