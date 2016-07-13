// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"
#include "escher/gl/unique_program.h"

namespace escher {

class BlitShader {
 public:
  BlitShader();
  ~BlitShader();

  bool Compile();

  const UniqueProgram& program() const { return program_; }

  // Uniforms
  GLint source() const { return source_; }

  // Attributes
  GLint position() const { return position_; }

 private:
  UniqueProgram program_;

  GLint position_ = -1;
  GLint source_ = -1;

  FTL_DISALLOW_COPY_AND_ASSIGN(BlitShader);
};

}  // namespace escher
