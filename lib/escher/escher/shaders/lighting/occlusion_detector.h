// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class OcclusionDetector {
 public:
  OcclusionDetector();
  ~OcclusionDetector();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint depth_map() const { return depth_map_; }
  GLint noise() const { return noise_; }
  GLint viewing_volume() const { return viewing_volume_; }
  GLint key_light() const { return key_light_; }

  // Attributes
  GLint position() const { return position_; }

  // Must match fragment shader.
  static const int kNoiseSize = 5;

 private:
  UniqueProgram program_;

  GLint depth_map_ = 0;
  GLint noise_ = 0;
  GLint viewing_volume_ = 0;
  GLint key_light_ = 0;
  GLint position_ = 0;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(OcclusionDetector);
};

}  // namespace escher
