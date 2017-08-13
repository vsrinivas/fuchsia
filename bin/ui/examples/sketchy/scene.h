// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/scene/client/resources.h"
#include "apps/mozart/lib/scene/client/session.h"
#include "apps/mozart/lib/sketchy/canvas.h"

namespace sketchy_example {

class Scene {
 public:
  Scene(mozart::client::Session* session, float width, float height);

  mozart::client::EntityNode* stroke_group_holder() {
    return &stroke_group_holder_;
  }

 private:
  mozart::client::DisplayCompositor compositor_;
  mozart::client::EntityNode stroke_group_holder_;
};

}  // namespace sketchy_client
