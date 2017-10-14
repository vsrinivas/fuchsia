// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include "lib/escher/geometry/types.h"
#include "lib/escher/scene/ambient_light.h"
#include "lib/escher/scene/directional_light.h"
#include "lib/escher/scene/viewing_volume.h"
#include "lib/fxl/macros.h"

namespace escher {

class Stage {
 public:
  Stage();
  ~Stage();

  const ViewingVolume& viewing_volume() const { return viewing_volume_; };
  void set_viewing_volume(ViewingVolume value);

  const DirectionalLight& key_light() const { return key_light_; }
  void set_key_light(DirectionalLight value) { key_light_ = std::move(value); }

  const AmbientLight& fill_light() const { return fill_light_; }
  void set_fill_light(AmbientLight value) { fill_light_ = std::move(value); }

  const vec4& clear_color() const { return clear_color_; };
  void set_clear_color(vec4 clear_color) { clear_color_ = clear_color; };
  void set_clear_color(vec3 clear_color) {
    set_clear_color(vec4(clear_color, 1.f));
  }

  float width() const { return viewing_volume_.width(); }
  float height() const { return viewing_volume_.height(); }

 private:
  ViewingVolume viewing_volume_;
  DirectionalLight key_light_;
  AmbientLight fill_light_;
  vec4 clear_color_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stage);
};

}  // namespace escher
