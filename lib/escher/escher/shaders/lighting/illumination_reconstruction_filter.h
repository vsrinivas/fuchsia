// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class IlluminationReconstructionFilter {
 public:
  IlluminationReconstructionFilter();
  ~IlluminationReconstructionFilter();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint illumination_map() const { return illumination_map_; }
  GLint tap_stride() const { return tap_stride_; }

  // Attributes
  GLint position() const { return position_; }

 private:
  UniqueProgram program_;

  GLint illumination_map_ = 0;
  GLint tap_stride_ = 0;
  GLint position_ = 0;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(IlluminationReconstructionFilter);
};

}  // namespace escher
