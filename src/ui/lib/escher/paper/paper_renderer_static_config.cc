// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"

namespace escher {
// List of all the shader paths used by PaperRenderer.
const std::vector<std::string> kPaperRendererShaderPaths = {
    "shaders/model_renderer/main.frag",
    "shaders/model_renderer/main.vert",
    "shaders/model_renderer/default_position.vert",
    "shaders/model_renderer/shadow_map_generation.frag",
    "shaders/model_renderer/shadow_map_lighting.frag",
    "shaders/model_renderer/wobble_position.vert",
    "shaders/paper/common/use.glsl",
    "shaders/paper/frag/main_ambient_light.frag",
    "shaders/paper/frag/main_point_light.frag",
    "shaders/paper/vert/compute_model_space_position.vert",
    "shaders/paper/vert/compute_world_space_position.vert",
    "shaders/paper/vert/main_shadow_volume_extrude.vert",
    "shaders/paper/vert/vertex_attributes.vert"};

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
                     {ShaderStage::kFragment, "shaders/model_renderer/main.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_UV", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        // TODO(fxbug.dev/7244): currently required by main.vert.
        {"NO_SHADOW_LIGHTING_PASS", "1"},
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

const ShaderProgramData kPointLightFalloffProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, "shaders/paper/frag/main_point_light.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_UV", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT_FALLOFF", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_POINT_LIGHTING", "1"},
    }),
};

const ShaderProgramData kShadowVolumeGeometryProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, ""}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_EXTRUSION", "1"},
    }),
};

const ShaderProgramData kShadowVolumeGeometryDebugProgramData = {
    .source_files = {{ShaderStage::kVertex, "shaders/model_renderer/main.vert"},
                     {ShaderStage::kFragment, "shaders/model_renderer/main.frag"}},
    .args = ShaderVariantArgs({
        {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
        {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
        {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
        {"SHADOW_VOLUME_EXTRUSION", "1"},
    }),
};
}  // namespace escher
