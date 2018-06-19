// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/shader_variant_args.h"

#include "lib/escher/util/hasher.h"

namespace escher {

Hash ShaderVariantArgs::GenerateHash() const {
  Hasher h;
  for (auto& def : definitions_) {
    h.string(def.first);
    h.string(def.second);
  }
  return h.value();
}

}  // namespace escher
