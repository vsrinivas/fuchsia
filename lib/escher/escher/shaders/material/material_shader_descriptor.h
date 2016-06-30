// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/scene/displacement.h"
#include "escher/scene/material.h"
#include "escher/shaders/material/modifier.h"

namespace escher {

// Describes the significant characteristics of a material and the context
// in which it is being drawn which influence the construction of a
// material shader.
struct MaterialShaderDescriptor {
  struct Hash;

  MaterialShaderDescriptor(BindingType color_binding_type,
                           Displacement::Type displacement,
                           Modifier::Mask mask);
  ~MaterialShaderDescriptor();

  bool operator==(const MaterialShaderDescriptor& other) const;
  size_t hash() const;

  BindingType color_binding_type;
  Displacement::Type displacement;
  Modifier::Mask mask;
};

struct MaterialShaderDescriptor::Hash {
  typedef MaterialShaderDescriptor argument_type;
  typedef size_t result_type;

  inline size_t operator()(const MaterialShaderDescriptor& descriptor) const {
    return descriptor.hash();
  }
};

}  // namespace escher
