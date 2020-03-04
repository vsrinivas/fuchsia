// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/shader_variant_args.h"

#include <set>

#include "src/ui/lib/escher/util/hasher.h"

namespace escher {

// static
std::vector<std::pair<std::string, std::string>> ShaderVariantArgs::Canonicalize(
    const std::vector<std::pair<std::string, std::string>>& defs) {
  auto result = defs;
  std::sort(result.begin(), result.end());
  typedef std::pair<std::string, std::string> Def;
  result.erase(std::unique(result.begin(), result.end(),
                           [](const Def& lhs, const Def& rhs) { return lhs.first == rhs.first; }),
               result.end());
  FXL_DCHECK(result.size() == defs.size()) << "shader args have duplicate definitions";
  return result;
}

Hash ShaderVariantArgs::GenerateHash() const {
  Hasher h;
  for (auto& def : definitions_) {
    h.string(def.first);
    h.string(def.second);
  }
  return h.value();
}

}  // namespace escher
