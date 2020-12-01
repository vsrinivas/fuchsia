// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"

namespace escher {
// List of all the shader paths used by PaperRenderer.
const std::vector<std::string> kPaperRendererShaderPaths = {
    "shaders/model_renderer/main.vert",
    "shaders/paper/common/use.glsl",
    "shaders/paper/frag/main_ambient_light.frag",
    "shaders/paper/frag/main_point_light.frag",
    "shaders/paper/vert/main_shadow_volume_extrude.vert",
    // TODO(jjosh): file bug to move these out of here, but still ensure they're built
    // for the Escher shader tests.  Suitable existing bug exists?
    "shaders/test/main.frag",
    "shaders/test/shadow_map_generation.frag",
    "shaders/test/shadow_map_lighting.frag",
};

const std::vector<std::string> kPaperRendererShaderSpirvPaths = {};

const ShaderProgramData kAmbientLightProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, "shaders/paper/frag/main_ambient_light.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_UV", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        // TODO(fxbug.dev/7244): currently required by main.vert.
        {"NO_SHADOW_LIGHTING_PASS", "1"},
    }),
};

const ShaderProgramData kNoLightingProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, "shaders/paper/frag/main_ambient_light.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_UV", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        // TODO(fxbug.dev/7244): currently required by main.vert.
        {"NO_SHADOW_LIGHTING_PASS", "1"},
        {"DISABLE_AMBIENT_LIGHT", "1"},
    }),
};

const ShaderProgramData kPointLightProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, "shaders/paper/frag/main_point_light.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_UV", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_POINT_LIGHTING", "1"},
    }),
};

const ShaderProgramData kShadowVolumeGeometryProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/paper/vert/main_shadow_volume_extrude.vert"},
                     {ShaderStage::kFragment, ""}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_EXTRUSION", "1"},
    }),
};

const ShaderProgramData kShadowVolumeGeometryDebugProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/paper/vert/main_shadow_volume_extrude.vert"},
                     {ShaderStage::kFragment, "shaders/paper/frag/main_ambient_light.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},  // for vertex shader, ignored by fragment shader
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_EXTRUSION", "1"},
        {"DISABLE_AMBIENT_LIGHT", "1"},
    }),
};
}  // namespace escher
