// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_SHADERS_UTIL_SPIRV_FILE_UTIL_H_
#define SRC_UI_LIB_ESCHER_SHADERS_UTIL_SPIRV_FILE_UTIL_H_

#include <vector>

#include "src/ui/lib/escher/vk/shader_variant_args.h"

namespace escher {
namespace shader_util {

// Writes the given spirv to a file on disk, whose name is generated based on the original shader
// name plus a hash value based on the provided ShaderVariantArgs.
bool WriteSpirvToDisk(const std::vector<uint32_t>& spirv, const ShaderVariantArgs& args,
                      const std::string& base_path, const std::string& shader_name);

}  // namespace shader_util
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SHADERS_UTIL_SPIRV_FILE_UTIL_H_
