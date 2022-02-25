// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/flatland_static_config.h"

namespace escher {
// List of all the shader paths used by FlatlandRenderer.
const std::vector<std::string> kFlatlandShaderPaths = {
    "shaders/flatland/flat_main.vert", "shaders/flatland/flat_main.frag",
    "shaders/flatland/flat_color_correction.vert", "shaders/flatland/flat_color_correction.frag"};

const ShaderProgramData kFlatlandStandardProgram = {
    .source_files = {{ShaderStage::kVertex, kFlatlandShaderPaths[0]},
                     {ShaderStage::kFragment, kFlatlandShaderPaths[1]}},
    .args = {}};

const ShaderProgramData kFlatlandColorConversionProgram = {
    .source_files = {{ShaderStage::kVertex, kFlatlandShaderPaths[2]},
                     {ShaderStage::kFragment, kFlatlandShaderPaths[3]}},
    .args = {}};

}  // namespace escher
