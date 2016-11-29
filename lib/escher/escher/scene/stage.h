// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include "ftl/macros.h"
#include "escher/scene/ambient_light.h"
#include "escher/scene/directional_light.h"
#include "escher/scene/viewing_volume.h"

namespace escher {

class Stage {
 public:
  Stage();
  ~Stage();

  void Resize(SizeI size,
              float device_pixel_ratio,
              SizeI viewport_offset = SizeI(0, 0));

  const ViewingVolume& viewing_volume() const { return viewing_volume_; };
  void set_viewing_volume(ViewingVolume value);

  const SizeI& physical_size() const { return physical_size_; }
  void set_physical_size(SizeI value) { physical_size_ = std::move(value); }

  const DirectionalLight& key_light() const { return key_light_; }
  void set_key_light(DirectionalLight value) { key_light_ = std::move(value); }

  const AmbientLight& fill_light() const { return fill_light_; }
  void set_fill_light(AmbientLight value) { fill_light_ = std::move(value); }

  const SizeI& viewport_offset() const { return viewport_offset_; }

  const vec3& clear_color() const { return clear_color_; };
  void set_clear_color(vec3 clear_color) { clear_color_ = clear_color; };

 private:
  SizeI physical_size_;
  SizeI viewport_offset_;
  ViewingVolume viewing_volume_;
  DirectionalLight key_light_;
  AmbientLight fill_light_;
  vec3 clear_color_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Stage);
};

}  // namespace escher
