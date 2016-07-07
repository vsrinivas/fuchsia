// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class IlluminationShader {
 public:
  IlluminationShader();
  ~IlluminationShader();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint color() const { return color_; }
  GLint illumination() const { return illumination_; }

  // Attributes
  GLint position() const { return position_; }

 private:
  UniqueProgram program_;

  GLint color_ = 0;
  GLint illumination_ = 0;
  GLint position_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(IlluminationShader);
};

}  // namespace escher
