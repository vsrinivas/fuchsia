// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>
#include <string>

#include "ftl/macros.h"

namespace escher {

// Generates snippets of GLSL.
class GLSLGenerator {
 public:
  GLSLGenerator();
  ~GLSLGenerator();

  void DefineSymbol(const std::string& symbol, const std::string& value);

  std::string GenerateCode();

 private:
  std::ostringstream code_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GLSLGenerator);
};

}  // namespace escher
