// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_display_list_builder.h"

#include <glm/gtx/transform.hpp>

#include "escher/impl/command_buffer.h"
#include "escher/impl/model_pipeline_cache.h"
#include "escher/impl/model_renderer.h"

namespace escher {
namespace impl {

namespace {
// TODO: should be queried from device.
constexpr vk::DeviceSize kMinUniformBufferOffsetAlignment = 256;

// Add a small fudge-factor so that we don't clip objects resting on the stage
// floor.  It can't be much smaller than this (0.00075 is too small for 16-bit
// depth formats).
constexpr float kStageFloorFudgeFactor = 0.0008f;
}  // namespace

ModelDisplayListBuilder::ModelDisplayListBuilder(
    vk::Device device,
    const Stage& stage,
    const Model& model,
    vec2 scale,
    bool use_material_textures,
    const TexturePtr& white_texture,
    const TexturePtr& illumination_texture,
    ModelData* model_data,
    ModelRenderer* renderer,
    ModelPipelineCache* pipeline_cache,
    uint32_t sample_count,
    bool use_depth_prepass)
    : device_(device),
      volume_(stage.viewing_volume()),
      stage_scale_(
          vec3(scale.x * 2.f / volume_.width(),
               scale.y * 2.f / volume_.height(),
               1.f / (volume_.depth_range() + kStageFloorFudgeFactor))),
      use_material_textures_(use_material_textures),
      white_texture_(white_texture),
      illumination_texture_(illumination_texture ? illumination_texture
                                                 : white_texture),
      renderer_(renderer),
      uniform_buffer_pool_(model_data->uniform_buffer_pool()),
      per_model_descriptor_set_pool_(
          model_data->per_model_descriptor_set_pool()),
      per_object_descriptor_set_pool_(
          model_data->per_object_descriptor_set_pool()),
      pipeline_cache_(pipeline_cache) {
  FTL_DCHECK(white_texture_);

  // These fields of the pipeline spec are the same for the entire display list.
  pipeline_spec_.sample_count = sample_count;
  pipeline_spec_.use_depth_prepass = use_depth_prepass;

  // Obtain a uniform buffer and write the PerModel data to it.
  PrepareUniformBufferForWriteOfSize(sizeof(ModelData::PerModel), 0);
  auto per_model =
      reinterpret_cast<ModelData::PerModel*>(uniform_buffer_->ptr());
  per_model->frag_coord_to_uv_multiplier =
      vec2(1.f / volume_.width(), 1.f / volume_.height());
  per_model->time = model.time();
  uniform_buffer_write_index_ += sizeof(ModelData::PerModel);

  // Obtain the single per-Model descriptor set.
  DescriptorSetAllocationPtr per_model_descriptor_set_allocation =
      per_model_descriptor_set_pool_->Allocate(1, nullptr);
  {
    // It would be inconvenient to set this in the initializer, so we briefly
    // cast it to set its permanent value.
    auto& unconst = const_cast<vk::DescriptorSet&>(per_model_descriptor_set_);
    unconst = per_model_descriptor_set_allocation->get(0);
  }
  resources_.push_back(std::move(per_model_descriptor_set_allocation));

  // Update each descriptor in the PerModel descriptor set.
  vk::WriteDescriptorSet writes[ModelData::PerModel::kDescriptorCount];

  auto& buffer_write = writes[0];
  buffer_write.dstSet = per_model_descriptor_set_;
  buffer_write.dstBinding = ModelData::PerModel::kDescriptorSetUniformBinding;
  buffer_write.dstArrayElement = 0;
  buffer_write.descriptorCount = 1;
  buffer_write.descriptorType = vk::DescriptorType::eUniformBuffer;
  vk::DescriptorBufferInfo buffer_info;
  buffer_info.buffer = uniform_buffer_->get();
  buffer_info.range = sizeof(ModelData::PerModel);
  buffer_info.offset = 0;
  buffer_write.pBufferInfo = &buffer_info;

  auto& image_write = writes[1];
  image_write.dstSet = per_model_descriptor_set_;
  image_write.dstBinding = ModelData::PerObject::kDescriptorSetSamplerBinding;
  image_write.dstArrayElement = 0;
  image_write.descriptorCount = 1;
  image_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  vk::DescriptorImageInfo image_info;
  image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  image_info.imageView = illumination_texture_->image_view();
  image_info.sampler = illumination_texture_->sampler();
  image_write.pImageInfo = &image_info;

  device_.updateDescriptorSets(2, writes, 0, nullptr);
}

// If |position| is already aligned to |alignment|, return it.  Otherwise,
// return the next-larger value that is so aligned.  If |alignment| is zero,
// |position| is always considered to be aligned.
static size_t AlignedToNext(size_t position, size_t alignment) {
  if (alignment && position % alignment) {
    size_t result = position + (alignment - position % alignment);
    // TODO: remove DCHECK and add unit test.
    FTL_DCHECK(result % alignment == 0);
    return result;
  }
  return position;
}

void ModelDisplayListBuilder::AddObject(const Object& object) {
  PrepareUniformBufferForWriteOfSize(sizeof(ModelData::PerObject),
                                     kMinUniformBufferOffsetAlignment);
  vk::DescriptorSet descriptor_set = ObtainPerObjectDescriptorSet();
  UpdateDescriptorSetForObject(object, descriptor_set);

  const bool is_clipper = !object.clipped_children().empty();
  const bool is_clippee = clip_depth_ > 0;

  // We can immediately create the display list item, even before we have
  // updated the descriptor set.
  ModelDisplayList::Item item;
  item.descriptor_sets[0] = descriptor_set;
  item.mesh = renderer_->GetMeshForShape(object.shape());
  pipeline_spec_.mesh_spec = item.mesh->spec;
  pipeline_spec_.shape_modifiers = object.shape().modifiers();
  pipeline_spec_.is_clippee = is_clippee;
  pipeline_spec_.clipper_state =
      is_clipper ? ModelPipelineSpec::ClipperState::kBeginClipChildren
                 : ModelPipelineSpec::ClipperState::kNoClipChildren;
  item.pipeline = pipeline_cache_->GetPipeline(pipeline_spec_);
  item.stencil_reference = clip_depth_;

  if (is_clipper) {
    // Drawing the item will increment the value in the stencil buffer.  Update
    // |clip_depth_| so that children can test against the correct value.
    items_.push_back(item);
    ++clip_depth_;

    // Recursively draw clipped children.
    for (auto& o : object.clipped_children()) {
      AddObject(o);
    }

    // Revert the stencil buffer to the previous state.
    // TODO: if we knew that no subsequent children were to be clipped, we
    // could avoid this.
    pipeline_spec_.mesh_spec = item.mesh->spec;
    pipeline_spec_.shape_modifiers = object.shape().modifiers();
    pipeline_spec_.is_clippee = is_clippee;
    pipeline_spec_.clipper_state =
        ModelPipelineSpec::ClipperState::kEndClipChildren;
    item.pipeline = pipeline_cache_->GetPipeline(pipeline_spec_);
    item.stencil_reference = clip_depth_;
    items_.push_back(std::move(item));
    --clip_depth_;
  } else {
    // Simply push the item.
    items_.push_back(std::move(item));
  }
}

void ModelDisplayListBuilder::UpdateDescriptorSetForObject(
    const Object& object,
    vk::DescriptorSet descriptor_set) {
  auto per_object = reinterpret_cast<ModelData::PerObject*>(
      &(uniform_buffer_->ptr()[uniform_buffer_write_index_]));
  *per_object = ModelData::PerObject();  // initialize with default values
  auto& transform = per_object->transform;
  auto& scale_x = transform[0][0];
  auto& scale_y = transform[1][1];
  auto& translate_x = transform[3][0];
  auto& translate_y = transform[3][1];
  auto& translate_z = transform[3][2];
  auto& color = per_object->color;

  // Push uniforms for scale/translation and color.
  scale_x = object.width() * stage_scale_.x;
  scale_y = object.height() * stage_scale_.y;
  translate_x = object.position().x * stage_scale_.x - 1.f;
  translate_y = object.position().y * stage_scale_.y - 1.f;
  // Convert "height above the stage" into "distance from the camera",
  // normalized to the range (0,1).  This is passed unaltered through the
  // vertex shader.  See the note above, where we set the viewport min/max
  // depth.
  translate_z =
      1.f - (object.position().z + kStageFloorFudgeFactor) * stage_scale_.z;

  if (object.rotation() != 0.f) {
    float pre_rot_translation_x = -object.rotation_point().x;
    float pre_rot_translation_y = -object.rotation_point().y;

    transform = glm::translate(
        transform,
        glm::vec3(-pre_rot_translation_x, -pre_rot_translation_y, 0.f));

    transform =
        glm::rotate(transform, object.rotation(), glm::vec3(0.f, 0.f, 1.f));

    transform = glm::translate(
        transform,
        glm::vec3(pre_rot_translation_x, pre_rot_translation_y, 0.f));
  }

  color = vec4(object.material()->color(), 1.f);  // always opaque

  // Find the texture to use, either the object's material's texture, or
  // the default texture if the material doesn't have one.
  vk::ImageView image_view;
  vk::Sampler sampler;
  if (auto& texture = object.material()->texture()) {
    if (!use_material_textures_) {
      // The object's material has a texture, but we choose not to use it.
      image_view = white_texture_->image_view();
      sampler = white_texture_->sampler();
    } else {
      image_view = object.material()->image_view();
      sampler = object.material()->sampler();
      textures_.push_back(texture);
    }
  } else {
    // No texture available.  Use white texture, so that object's color shows.
    image_view = white_texture_->image_view();
    sampler = white_texture_->sampler();
  }

  if (object.shape().modifiers() | ShapeModifier::kWobble) {
    auto wobble = object.shape_modifier_data<ModifierWobble>();
    per_object->wobble = wobble ? *wobble : ModifierWobble();
  }

  // Update each descriptor in the PerObject descriptor set.
  {
    // A pair of writes; order doesn't matter.
    vk::WriteDescriptorSet writes[ModelData::PerObject::kDescriptorCount];

    auto& buffer_write = writes[0];
    buffer_write.dstSet = descriptor_set;
    buffer_write.dstBinding =
        ModelData::PerObject::kDescriptorSetUniformBinding;
    buffer_write.dstArrayElement = 0;
    buffer_write.descriptorCount = 1;
    buffer_write.descriptorType = vk::DescriptorType::eUniformBuffer;
    vk::DescriptorBufferInfo buffer_info;
    buffer_info.buffer = uniform_buffer_->get();
    buffer_info.range = sizeof(ModelData::PerObject);
    buffer_info.offset = uniform_buffer_write_index_;
    buffer_write.pBufferInfo = &buffer_info;

    auto& image_write = writes[1];
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = ModelData::PerObject::kDescriptorSetSamplerBinding;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = 1;
    image_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    vk::DescriptorImageInfo image_info;
    image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    image_info.imageView = image_view;
    image_info.sampler = sampler;
    image_write.pImageInfo = &image_info;

    device_.updateDescriptorSets(2, writes, 0, nullptr);
  }

  uniform_buffer_write_index_ += sizeof(ModelData::PerObject);
}

ModelDisplayListPtr ModelDisplayListBuilder::Build(
    CommandBuffer* command_buffer) {
  for (auto& uniform_buffer : uniform_buffers_) {
    vk::BufferMemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = uniform_buffer->get();
    barrier.offset = 0;
    barrier.size = uniform_buffer->size();

    command_buffer->get().pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,
        vk::PipelineStageFlagBits::eVertexShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        vk::DependencyFlags(), 0, nullptr, 1, &barrier, 0, nullptr);

    // The display list still needs to retain the buffer, and since it no
    // longer needs special treatment, add it in with the other resources.
    resources_.push_back(std::move(uniform_buffer));
  }
  uniform_buffers_.clear();

  auto display_list = ftl::MakeRefCounted<ModelDisplayList>(
      renderer_->life_preserver(), per_model_descriptor_set_, std::move(items_),
      std::move(textures_), std::move(resources_));
  command_buffer->KeepAlive(display_list);
  return display_list;
}

vk::DescriptorSet ModelDisplayListBuilder::ObtainPerObjectDescriptorSet() {
  if (!per_object_descriptor_set_allocation_ ||
      per_object_descriptor_set_index_ >=
          per_object_descriptor_set_allocation_->size()) {
    per_object_descriptor_set_index_ = 0;

    constexpr uint32_t kReasonableDescriptorSetAllocationCount = 100;
    per_object_descriptor_set_allocation_ =
        per_object_descriptor_set_pool_->Allocate(
            kReasonableDescriptorSetAllocationCount, nullptr);
    resources_.push_back(per_object_descriptor_set_allocation_);
  }

  vk::DescriptorSet ds = per_object_descriptor_set_allocation_->get(
      per_object_descriptor_set_index_);
  ++per_object_descriptor_set_index_;

  return ds;
}

void ModelDisplayListBuilder::PrepareUniformBufferForWriteOfSize(
    size_t size,
    size_t alignment) {
  uniform_buffer_write_index_ =
      AlignedToNext(uniform_buffer_write_index_, alignment);

  if (!uniform_buffer_ ||
      uniform_buffer_write_index_ + size > uniform_buffer_->size()) {
    uniform_buffer_ = uniform_buffer_pool_->Allocate();
    uniform_buffer_write_index_ = 0;
    uniform_buffers_.push_back(uniform_buffer_);
  }
}

}  // namespace impl
}  // namespace escher
