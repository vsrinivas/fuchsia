// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "escher/base/macros.h"
#include "escher/scene/material.h"
#include "escher/shaders/material/material_shader.h"
#include "escher/shaders/material/modifier.h"

namespace escher {

// Creates material shader programs on demand.
class MaterialShaderFactory {
 public:
  MaterialShaderFactory();
  ~MaterialShaderFactory();

  // TODO(jeffbrown): Add the ability to flush shaders on demand.

  const MaterialShader* GetShader(const Material& material,
                                  const Modifier& modifier);

 private:
  std::unordered_map<MaterialShaderDescriptor,
                     std::unique_ptr<MaterialShader>,
                     MaterialShaderDescriptor::Hash>
      shaders_;

  ESCHER_DISALLOW_COPY_AND_ASSIGN(MaterialShaderFactory);
};

}  // namespace escher
