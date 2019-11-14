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

// Reads the spirv for a shader given the original file name and a list of args. Generates the
// spirv file name based on the hash it calculates and sees if there is anything on disk to read.
// If so, the function returns true and the code is stored in the out_spirv parameter.
bool ReadSpirvFromDisk(const ShaderVariantArgs& args, const std::string& base_path,
                       const std::string& shader_name, std::vector<uint32_t>* out_spirv);

}  // namespace shader_util
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_SHADERS_UTIL_SPIRV_FILE_UTIL_H_
