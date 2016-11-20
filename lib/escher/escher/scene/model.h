// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "ftl/macros.h"
#include "escher/scene/object.h"

namespace escher {

// The model to render.
//
// TODO(jeffbrown): This currently only contains a vector of objects to be
// rendered but later on we may store additional resources used to
// interpret the contents of the model (such as tunable parameters).
class Model {
 public:
  Model();
  explicit Model(std::vector<Object> objects);
  ~Model();

  Model(Model&& other);
  Model& operator=(Model&& other);

  // Objects in back to front draw order.
  const std::vector<Object>& objects() const { return objects_; }

  // Time in seconds.
  float time() const { return time_; }
  void set_time(float time) { time_ = time; }

  // TODO(jjosh): Used to parameterize DepthBasedBlur.  Need an API for this.
  float blur_plane_height() const { return blur_plane_height_; }
  void set_blur_plane_height(float height) { blur_plane_height_ = height; }

 private:
  std::vector<Object> objects_;
  float time_ = 0.0f;
  float blur_plane_height_ = 0.0f;

  FTL_DISALLOW_COPY_AND_ASSIGN(Model);
};

}  // namespace escher
