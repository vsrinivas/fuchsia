// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_SHADER_PROGRAM_FACTORY_H_
#define LIB_ESCHER_VK_SHADER_PROGRAM_FACTORY_H_

#include "lib/escher/base/trait.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/util/enum_count.h"
#include "lib/escher/vk/shader_stage.h"
#include "lib/escher/vk/shader_variant_args.h"

namespace escher {

// ShaderProgramFactory is a |Trait| that clients use to obtain ShaderPrograms.
// Subclasses must override GetProgram(), and will typically lazily-generate and
// cache these programs.
class ShaderProgramFactory : public Trait {
 public:
  // Return a compute program whose code is specified by |compute_shader_path|.
  ShaderProgramPtr GetComputeProgram(std::string compute_shader_path,
                                     ShaderVariantArgs args = {});

  // Return a graphics program which has only vertex and fragment shader stages;
  // The fragment shader path may be empty: this is used for depth-only passes.
  ShaderProgramPtr GetGraphicsProgram(std::string vertex_shader_path,
                                      std::string fragment_shader_path,
                                      ShaderVariantArgs args = {});

  // Return a graphics program containing all shader stages that a non-empty
  // path is provided for.
  ShaderProgramPtr GetGraphicsProgram(
      std::string vertex_shader_path,
      std::string tessellation_control_shader_path,
      std::string tessellation_evaluation_shader_path,
      std::string geometry_shader_path, std::string fragment_shader_path,
      ShaderVariantArgs args);

 protected:
  virtual ~ShaderProgramFactory();

  // Subclasses must implement this.  The array index of each path corresponds
  // to a value in the ShaderStage enum; each non-empty path provides the source
  // code for the corresponding shader stage.
  virtual ShaderProgramPtr GetProgram(
      const std::string shader_paths[EnumCount<ShaderStage>()],
      ShaderVariantArgs args) = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_SHADER_PROGRAM_FACTORY_H_
