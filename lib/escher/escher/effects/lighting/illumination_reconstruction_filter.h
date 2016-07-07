// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class IlluminationReconstructionFilter {
 public:
  IlluminationReconstructionFilter();
  ~IlluminationReconstructionFilter();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint illumination() const { return illumination_; }
  GLint stride() const { return stride_; }

  // Attributes
  GLint position() const { return position_; }

 private:
  UniqueProgram program_;

  GLint illumination_ = 0;
  GLint stride_ = 0;
  GLint position_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(IlluminationReconstructionFilter);
};

}  // namespace escher
