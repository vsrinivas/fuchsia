// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_SHADER_MODULE_H_
#define LIB_ESCHER_VK_SHADER_MODULE_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/third_party/granite/vk/shader_module_resource_layout.h"
#include "lib/escher/vk/shader_stage.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace escher {

class ShaderModule;
using ShaderModulePtr = fxl::RefPtr<ShaderModule>;

// Listen for changes in a ShaderModule that occur when new SPIR-V is provided
// to it.
class ShaderModuleListener {
 public:
  virtual void OnShaderModuleUpdated(ShaderModule* shader_module) = 0;
};

// Base class that knows hows to wrap SPIR-V code into a vk::ShaderModule and
// notify listeners so that e.g. vk::Pipelines can be invalidated/regenerated.
// Subclasses should call RecreateModuleFromSpirvAndNotifyListeners() whenever
// the input SPIR-V code changes.
//
// The primary design goal is to decouple use of binary SPIR-V code (i.e. to
// build vk::Pipelines), from how the SPIR-V code is produced.  For example,
// see ShaderModuleTemplate, which generates variants of the template by
// compiling GLSL code with different preprocessor definitions.
class ShaderModule : public fxl::RefCountedThreadSafe<ShaderModule> {
 public:
  ShaderModule(vk::Device device, ShaderStage shader_stage);
  virtual ~ShaderModule();

  // Return the shader stage that this module should be used for.
  ShaderStage shader_stage() const { return stage_; }

  // Return true if a valid vk::ShaderModule is available, and false otherwise.
  bool is_valid() const { return module_; }

  // Return the most up-to-date vk::ShaderModule. Clients must ensure that the
  // module is_valid() before calling.
  const vk::ShaderModule& vk() const {
    FXL_DCHECK(is_valid());
    return module_;
  }

  // Add a listener. If is_valid(), then listener->OnShaderModuleUpdated() will
  // be called immediately.
  void AddShaderModuleListener(ShaderModuleListener* listener);

  // Listeners are responsible for removing themselves before the ShaderModule
  // is destroyed.
  void RemoveShaderModuleListener(ShaderModuleListener* listener);

  // Return the module's resource layout. Escher clients never need to call
  // this; it is used internally by ShaderProgram.
  const impl::ShaderModuleResourceLayout& shader_module_resource_layout()
      const {
    FXL_DCHECK(is_valid());
    return layout_;
  }

 protected:
  // Subclasses should call this when new SPIR-V is available.
  void RecreateModuleFromSpirvAndNotifyListeners(std::vector<uint32_t> spirv);

 private:
  vk::Device device_;
  ShaderStage stage_;
  vk::ShaderModule module_;
  std::vector<ShaderModuleListener*> listeners_;
  impl::ShaderModuleResourceLayout layout_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_SHADER_MODULE_H_
