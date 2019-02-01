// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_WOBBLE_MODIFIER_ABSORBER_H_
#define LIB_ESCHER_IMPL_WOBBLE_MODIFIER_ABSORBER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/compute_shader.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/object.h"
#include "lib/fxl/macros.h"

namespace escher {
namespace impl {

// A helper class that absorbs wobble modifier into the vertex buffer.
// Not thread-safe.
class WobbleModifierAbsorber {
 public:
  WobbleModifierAbsorber(EscherWeakPtr escher);
  ~WobbleModifierAbsorber() {}
  void AbsorbWobbleIfAny(Model* model);

 private:
  std::unique_ptr<ComputeShader> NewKernel();
  BufferPtr NewUniformBuffer(vk::DeviceSize size);
  void ApplyBarrierForUniformBuffer(CommandBuffer* command_buffer,
                                    const BufferPtr& buffer_ptr);

  const EscherWeakPtr escher_;
  const VulkanContext& vulkan_context_;
  CommandBufferPool* const command_buffer_pool_;
  GlslToSpirvCompiler* const compiler_;
  GpuAllocator* const allocator_;
  ResourceRecycler* const recycler_;
  const std::unique_ptr<ComputeShader> kernel_;

  std::array<uint32_t, 1> push_constants_;
  const BufferPtr per_model_uniform_buffer_;
  ModelData::PerModel* const per_model_uniform_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WobbleModifierAbsorber);
};

}  // namespace impl
}  // namespace escher
#endif  // LIB_ESCHER_IMPL_WOBBLE_MODIFIER_ABSORBER_H_
