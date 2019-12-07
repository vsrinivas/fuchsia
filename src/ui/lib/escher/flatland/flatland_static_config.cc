// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/flatland_static_config.h"

namespace escher {
// List of all the shader paths used by FlatlandRenderer.
const std::vector<std::string> kFlatlandShaderPaths = {"shaders/flatland/flat_main.vert",
                                                       "shaders/flatland/flat_main.frag"};

const ShaderProgramData kFlatlandStandardProgram = {
    .source_files = {{ShaderStage::kVertex, "shaders/flatland/flat_main.vert"},
                     {ShaderStage::kFragment, "shaders/flatland/flat_main.frag"}},
    .args = {}};

}  // namespace escher
