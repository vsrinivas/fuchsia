// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_SHADER_VARIANT_ARGS_H_
#define SRC_UI_LIB_ESCHER_VK_SHADER_VARIANT_ARGS_H_

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/ui/lib/escher/util/hashable.h"
#include "src/ui/lib/escher/vk/shader_stage.h"

namespace escher {

// Contains preprocessor definitions to be passed to a shader compiler in order
// to obtain a variant of a ShaderProgram or ShaderModule.  The consumers of
// these args are not required to sort the definitions into a canonical order;
// e.g. if you pass {FOO=1,BAR=2} and {BAR=2,FOO=1} to a ShaderProgramFactory,
// it is likely to generate two different program variants that compile to
// identical VkPipelines.
class ShaderVariantArgs : public Hashable {
 public:
  explicit ShaderVariantArgs(std::vector<std::pair<std::string, std::string>> defs)
      : definitions_(Canonicalize(defs)) {}

  ShaderVariantArgs() {}
  ShaderVariantArgs(ShaderVariantArgs&& other) = default;
  ShaderVariantArgs(const ShaderVariantArgs& other) = default;

  ShaderVariantArgs& operator=(ShaderVariantArgs&& other) = default;

  // |Hashable|.
  bool operator==(const ShaderVariantArgs& other) const;
  bool operator!=(const ShaderVariantArgs& other) const;

  // Get/set the name/value pairs of preprocessor definitions that are used in
  // this variant of a ShaderProgram or ShaderModule.
  const std::vector<std::pair<std::string, std::string>>& definitions() const;
  void set_definitions(std::vector<std::pair<std::string, std::string>> defs);

 private:
  // |Hashable|.
  Hash GenerateHash() const override;

  // Canonicalize removes duplicates and sorts definitions to ensure a stable hash across build
  // types and platforms.
  static std::vector<std::pair<std::string, std::string>> Canonicalize(
      const std::vector<std::pair<std::string, std::string>>& defs);

  std::vector<std::pair<std::string, std::string>> definitions_;
};

// Inline function definitions.

inline bool ShaderVariantArgs::operator==(const ShaderVariantArgs& other) const {
  return hash() == other.hash() && definitions_ == other.definitions_;
}

inline bool ShaderVariantArgs::operator!=(const ShaderVariantArgs& other) const {
  return !(*this == other);
}

inline const std::vector<std::pair<std::string, std::string>>& ShaderVariantArgs::definitions()
    const {
  return definitions_;
}

inline void ShaderVariantArgs::set_definitions(
    std::vector<std::pair<std::string, std::string>> defs) {
  definitions_ = Canonicalize(defs);
  InvalidateHash();
}

typedef std::map<ShaderStage, std::string> StageMap;

// Struct to house data for a shader program including its source
// files and its
struct ShaderProgramData {
  StageMap source_files;
  ShaderVariantArgs args = {};
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_SHADER_VARIANT_ARGS_H_
