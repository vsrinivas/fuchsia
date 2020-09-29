// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shaders/util/spirv_file_util.h"

namespace escher {
namespace {

// Given a path name for a variant shader and its args, generate a new hashed name for that
// shader's spirv code to be saved on disk.
// For example if the shader name was "main.vert" and the hash is "9731555" then the final
// hashed name will be "main_vert9731555.spirv".
std::string GenerateHashedSpirvName(const std::string& path, const ShaderVariantArgs& args) {
  uint64_t hash_value = args.hash().val;
  std::string result = path + std::to_string(hash_value);
  std::replace(result.begin(), result.end(), '.', '_');
  std::replace(result.begin(), result.end(), '/', '_');
  return result + ".spirv";
}
}  // namespace

namespace shader_util {
bool WriteSpirvToDisk(const std::vector<uint32_t>& spirv, const ShaderVariantArgs& args,
                      const std::string& base_path, const std::string& shader_name) {
  auto hash_name = GenerateHashedSpirvName(shader_name, args);
  auto full_path = base_path + hash_name;
  FILE* fp = fopen(full_path.c_str(), "wb");
  if (fp) {
    fwrite(spirv.data(), sizeof(uint32_t), spirv.size(), fp);
    fclose(fp);
    return true;
  } else {
    FX_LOGS(ERROR) << "Could not write file: " << full_path;
  }

  return false;
}

bool ReadSpirvFromDisk(const ShaderVariantArgs& args, const std::string& base_path,
                       const std::string& shader_name, std::vector<uint32_t>* out_spirv) {
  FX_DCHECK(out_spirv);
  auto hash_name = GenerateHashedSpirvName(shader_name, args);
  auto full_path = base_path + hash_name;
  FILE* fp = fopen(full_path.c_str(), "rb");
  if (fp) {
    std::size_t binary_size;
    fseek(fp, 0, SEEK_END);
    binary_size = ftell(fp);
    rewind(fp);

    // File was empty.
    if (binary_size == 0) {
      return false;
    }

    size_t num_elements = binary_size / sizeof(uint32_t);
    out_spirv->resize(num_elements);
    size_t num_read = fread(out_spirv->data(), sizeof(uint32_t), num_elements, fp);
    return num_read == num_elements;
  }

  return false;
}

bool SpirvExistsOnDisk(const ShaderVariantArgs& args, const std::string& abs_root,
                       const std::string& shader_name, const std::vector<uint32_t>& spirv) {
  bool should_write_spirv = true;
  std::vector<uint32_t> existing_spirv;
  if (shader_util::ReadSpirvFromDisk(args, abs_root, shader_name, &existing_spirv)) {
    if (existing_spirv == spirv) {
      should_write_spirv = false;
    }
  }
  return should_write_spirv;
}

}  // namespace shader_util
}  // namespace escher
