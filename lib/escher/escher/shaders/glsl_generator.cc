// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/glsl_generator.h"

namespace escher {

GLSLGenerator::GLSLGenerator() {}

GLSLGenerator::~GLSLGenerator() {}

void GLSLGenerator::DefineSymbol(const std::string& symbol,
                                 const std::string& value) {
  code_ << "#define " << symbol << " " << value << std::endl;
}

std::string GLSLGenerator::GenerateCode() {
  return code_.str();
}

}  // namespace escher
