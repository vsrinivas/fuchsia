// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include "escher/base/macros.h"
#include "escher/scene/ambient_light.h"
#include "escher/scene/directional_light.h"
#include "escher/scene/viewing_volume.h"

namespace escher {

class Stage {
 public:
  Stage();
  ~Stage();

  const ViewingVolume& viewing_volume() const { return viewing_volume_; };
  void set_viewing_volume(ViewingVolume value) {
    viewing_volume_ = std::move(value);
  }

  const SizeI& size() const { return viewing_volume_.size(); }
  void SetSize(SizeI size);

  const DirectionalLight& key_light() const { return key_light_; }
  void set_key_light(DirectionalLight value) { key_light_ = std::move(value); }

  const AmbientLight& fill_light() const { return fill_light_; }
  void set_fill_light(AmbientLight value) { fill_light_ = std::move(value); }

 private:
  ViewingVolume viewing_volume_;
  DirectionalLight key_light_;
  AmbientLight fill_light_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(Stage);
};

}  // namespace escher
