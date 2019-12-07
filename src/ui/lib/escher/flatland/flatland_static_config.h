// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_
#define SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_

#include "src/ui/lib/escher/vk/shader_variant_args.h"

namespace escher {

// List of all the shader paths used by FlatlandRenderer
extern const std::vector<std::string> kFlatlandShaderPaths;

// List of flatland renderer shader program data.
extern const ShaderProgramData kFlatlandStandardProgram;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLATLAND_FLATLAND_STATIC_CONFIG_H_
