// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_
#define SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/vk/shader_variant_args.h"

namespace escher {

// List of all the shader paths used by FlatlandRenderer
extern const std::vector<std::string> kFlatlandShaderPaths;

// List of flatland renderer shader program data.
extern const ShaderProgramData kFlatlandStandardProgram;

// List of flatland renderer shader program data.
extern const ShaderProgramData kFlatlandColorConversionProgram;

// Color conversion parameters used inside the color conversion shader
// program by the RectangleCompositor.
struct ColorConversionParams {
  alignas(16) glm::mat4 matrix = glm::mat4(1.0);
  alignas(16) glm::vec4 preoffsets = glm::vec4(0.f);
  alignas(16) glm::vec4 postoffsets = glm::vec4(0.f);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_
