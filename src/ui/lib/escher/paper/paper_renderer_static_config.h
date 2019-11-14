// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_STATIC_CONFIG_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_STATIC_CONFIG_H_

#include "src/ui/lib/escher/vk/shader_variant_args.h"

// The purpose of this file is to share data between the PaperRenderer
// implementation and other clients who need to know this data, such
// as the offline shader compiler.
namespace escher {

// List of all the shader paths used by PaperRenderer.
extern const std::vector<std::string> kPaperRendererShaderPaths;
extern const std::vector<std::string> kPaperRendererShaderSpirvPaths;

// List of paper renderer shader program data.
// NOTE: If these are modified in any way, or if a new constant
// is added, the clients that rely on this data will not be
// automatically updated to take that into account.
extern const ShaderProgramData kAmbientLightProgramData;
extern const ShaderProgramData kNoLightingProgramData;
extern const ShaderProgramData kPointLightProgramData;
extern const ShaderProgramData kPointLightFalloffProgramData;
extern const ShaderProgramData kShadowVolumeGeometryProgramData;
extern const ShaderProgramData kShadowVolumeGeometryDebugProgramData;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_STATIC_CONFIG_H_
