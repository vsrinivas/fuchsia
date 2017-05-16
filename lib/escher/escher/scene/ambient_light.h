// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

// Ambient light is emitted omnidirectionally from infinity.
class AmbientLight {
 public:
  AmbientLight();
  explicit AmbientLight(float intensity);
  ~AmbientLight();

  // The amount of light emitted.
  // TODO(abarth): In what units?
  float intensity() const { return intensity_; }

 private:
  float intensity_ = 0.0f;
};

}  // namespace escher
