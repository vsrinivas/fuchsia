// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/wobble_modifier_absorber.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/resources/resource_recycler.h"

namespace escher {
namespace impl {

namespace {

// TODO: Adjust the number, as well as the one in shader.
constexpr uint32_t kLocalSize = 32;

constexpr char g_compute_wobble_src[] = R"GLSL(
    #version 450
    #extension GL_ARB_separate_shader_objects : enable

    layout(push_constant) uniform PushConstants {
      uint num_vertices;
    };

    // Attribute order must match that is defined in model_data.h
    struct Vertex {
      float pos_x;
      float pos_y;
      float offset_x;
      float offset_y;
      float u;
      float v;
      float perimeter;
    };

    layout(binding = 0) buffer Vertices {
      Vertex vertices[];
    };

    layout(local_size_x = 32) in;

    layout(binding = 1) uniform PerModel {
      vec2 frag_coord_to_uv_multiplier;
      float time;
      vec3 ambient_light_intensity;
      vec3 direct_light_intensity;
      vec2 shadow_map_uv_multiplier;
    };

    layout(binding = 2) uniform PerObject {
      mat4 transform;
      vec4 color;
      // Corresponds to ModifierWobble::SineParams[0].
      float speed_0;
      float amplitude_0;
      float frequency_0;
      // Corresponds to ModifierWobble::SineParams[1].
      float speed_1;
      float amplitude_1;
      float frequency_1;
      // Corresponds to ModifierWobble::SineParams[2].
      float speed_2;
      float amplitude_2;
      float frequency_2;
      // TODO: for some reason, I can't say:
      //   SineParams sine_params[kNumSineParams];
      // nor:
      //   SineParams sine_params_0;
      //   SineParams sine_params_1;
      //   SineParams sine_params_2;
      // ... if I try, the GLSL compiler produces SPIR-V, but the "SC"
      // validation layer complains when trying to create a vk::ShaderModule
      // from that SPIR-V.  Note: if we ignore the warning and proceed, nothing
      // explodes.  Nevertheless, we'll leave it this way for now, to be safe.
    };

    // TODO: workaround.  See discussion in PerObject struct, above.
    float EvalSineParams_0(uint idx) {
      float arg = frequency_0 * vertices[idx].perimeter + speed_0 * time;
      return amplitude_0 * sin(arg);
    }
    float EvalSineParams_1(uint idx) {
      float arg = frequency_1 * vertices[idx].perimeter + speed_1 * time;
      return amplitude_1 * sin(arg);
    }
    float EvalSineParams_2(uint idx) {
      float arg = frequency_2 * vertices[idx].perimeter + speed_2 * time;
      return amplitude_2 * sin(arg);
    }

    void main() {
      uint idx = gl_GlobalInvocationID.x;
      if (idx >= num_vertices)
        return;

      // TODO: workaround.  See discussion in PerObject struct, above.
      float offset_scale =

          EvalSineParams_0(idx) + EvalSineParams_1(idx) + EvalSineParams_2(idx);
      vertices[idx].pos_x =
          vertices[idx].pos_x + offset_scale * vertices[idx].offset_x;
      vertices[idx].pos_y =
          vertices[idx].pos_y + offset_scale * vertices[idx].offset_y;
    }
    )GLSL";

}  // namespace

WobbleModifierAbsorber::WobbleModifierAbsorber(EscherWeakPtr weak_escher)
    : escher_(std::move(weak_escher)),
      vulkan_context_(escher_->vulkan_context()),
      command_buffer_pool_(escher_->command_buffer_pool()),
      compiler_(escher_->glsl_compiler()),
      allocator_(escher_->gpu_allocator()),
      recycler_(escher_->resource_recycler()),
      kernel_(NewKernel()),
      per_model_uniform_buffer_(NewUniformBuffer(sizeof(ModelData::PerModel))),
      per_model_uniform_data_(reinterpret_cast<ModelData::PerModel*>(
          per_model_uniform_buffer_->host_ptr())) {}

void WobbleModifierAbsorber::AbsorbWobbleIfAny(Model* model) {
  // frag_coord_to_uv_multiplier is not used; won't populate.
  per_model_uniform_data_->time = model->time();
  bool is_per_model_uniform_buffer_barrier_applied = false;

  for (auto& object : model->mutable_objects()) {
    if (!(object.shape().modifiers() & ShapeModifier::kWobble)) {
      continue;
    }

    auto& vertex_buffer = object.shape().mesh()->vertex_buffer();
    auto compute_buffer =
        Buffer::New(recycler_, allocator_, vertex_buffer->size(),
                    vk::BufferUsageFlagBits::eVertexBuffer |
                        vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eTransferDst,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
    // TODO(longqic): Do not allocate a new uniform buffer for each new object.
    // See ModelDisplayListBuilder::PrepareUniformBufferForWriteOfSize().
    auto per_object_uniform_buffer =
        NewUniformBuffer(sizeof(ModelData::PerObject));
    auto per_object_uniform_data = reinterpret_cast<ModelData::PerObject*>(
        per_object_uniform_buffer->host_ptr());

    // For memory transfer.
    CommandBuffer* command_buffer = command_buffer_pool_->GetCommandBuffer();
    command_buffer->KeepAlive(vertex_buffer);
    command_buffer->KeepAlive(compute_buffer);

    vk::BufferCopy region(0, 0, vertex_buffer->size());
    command_buffer->vk().copyBuffer(vertex_buffer->vk(), compute_buffer->vk(),
                                    1, &region);

    SemaphorePtr vertex_buffer_ready = vertex_buffer->TakeWaitSemaphore();
    command_buffer->AddWaitSemaphore(vertex_buffer_ready,
                                     vk::PipelineStageFlagBits::eTransfer);
    SemaphorePtr compute_buffer_ready = Semaphore::New(vulkan_context_.device);
    command_buffer->AddSignalSemaphore(compute_buffer_ready);
    command_buffer->Submit(vulkan_context_.transfer_queue, nullptr);

    // For compute.
    command_buffer = command_buffer_pool_->GetCommandBuffer();
    command_buffer->KeepAlive(compute_buffer);
    command_buffer->KeepAlive(per_object_uniform_buffer);

    if (!is_per_model_uniform_buffer_barrier_applied) {
      ApplyBarrierForUniformBuffer(command_buffer, per_model_uniform_buffer_);
      is_per_model_uniform_buffer_barrier_applied = true;
    }

    // Transform and color are not used; won't populate.
    per_object_uniform_data->wobble =
        *object.shape_modifier_data<ModifierWobble>();
    ApplyBarrierForUniformBuffer(command_buffer, per_object_uniform_buffer);

    uint32_t num_vertices = object.shape().mesh()->num_vertices();
    uint32_t group_count =
        num_vertices / kLocalSize + num_vertices % kLocalSize;
    push_constants_[0] = static_cast<uint32_t>(compute_buffer->size());
    kernel_->Dispatch(
        std::vector<TexturePtr>{},
        std::vector<BufferPtr>{compute_buffer, per_model_uniform_buffer_,
                               per_object_uniform_buffer},
        command_buffer, group_count, 1, 1, push_constants_.data());

    SemaphorePtr absorbed = Semaphore::New(vulkan_context_.device);
    command_buffer->AddWaitSemaphore(compute_buffer_ready,
                                     vk::PipelineStageFlagBits::eComputeShader);
    command_buffer->AddSignalSemaphore(absorbed);

    MeshPtr original_mesh = object.shape().mesh();
    MeshPtr modified_mesh = fxl::MakeRefCounted<Mesh>(
        recycler_, original_mesh->spec(), original_mesh->bounding_box(),
        original_mesh->num_vertices(), original_mesh->num_indices(),
        compute_buffer, original_mesh->index_buffer());
    object.mutable_shape().set_mesh(modified_mesh);
    modified_mesh->SetWaitSemaphore(absorbed);
    command_buffer->Submit(vulkan_context_.queue, nullptr);

    object.remove_shape_modifier<ModifierWobble>();
  }
}

std::unique_ptr<ComputeShader> WobbleModifierAbsorber::NewKernel() {
  return std::make_unique<ComputeShader>(
      escher_, std::vector<vk::ImageLayout>{},
      std::vector<vk::DescriptorType>{vk::DescriptorType::eStorageBuffer,
                                      vk::DescriptorType::eUniformBuffer,
                                      vk::DescriptorType::eUniformBuffer},
      sizeof(uint32_t) * push_constants_.size(), g_compute_wobble_src);
}

BufferPtr WobbleModifierAbsorber::NewUniformBuffer(vk::DeviceSize size) {
  return Buffer::New(recycler_, allocator_, size,
                     vk::BufferUsageFlagBits::eUniformBuffer,
                     vk::MemoryPropertyFlagBits::eHostVisible);
}

void WobbleModifierAbsorber::ApplyBarrierForUniformBuffer(
    CommandBuffer* command_buffer, const BufferPtr& buffer_ptr) {
  vk::BufferMemoryBarrier barrier;
  barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = buffer_ptr->vk();
  barrier.offset = 0;
  barrier.size = buffer_ptr->size();
  command_buffer->vk().pipelineBarrier(
      vk::PipelineStageFlagBits::eHost,
      vk::PipelineStageFlagBits::eVertexShader |
          vk::PipelineStageFlagBits::eFragmentShader,
      vk::DependencyFlags(), 0, nullptr, 1, &barrier, 0, nullptr);
}

}  // namespace impl
}  // namespace escher
