// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class IlluminationShader {
 public:
  IlluminationShader();
  ~IlluminationShader();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint scene() const { return scene_; }
  GLint lighting() const { return lighting_; }

  // Attributes
  GLint position() const { return position_; }

 private:
  UniqueProgram program_;

  GLint scene_ = 0;
  GLint lighting_ = 0;
  GLint position_ = 0;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(IlluminationShader);
};

}  // namespace escher
