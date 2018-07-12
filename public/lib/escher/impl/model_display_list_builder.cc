// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_display_list_builder.h"

#include <glm/gtx/transform.hpp>

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/model_pipeline_cache.h"
#include "lib/escher/impl/model_render_pass.h"
#include "lib/escher/impl/model_renderer.h"
#include "lib/escher/impl/z_sort.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/util/align.h"

namespace escher {
namespace impl {

namespace {

// TODO(ES-102): should be queried from device.
constexpr vk::DeviceSize kMinUniformBufferOffsetAlignment = 256;

}  // namespace

static mat4 AdjustCameraTransform(const Stage& stage, const Camera& camera,
                                  float scale) {
  // Adjust projection matrix to support downsampled render passes.
  mat4 scale_adjustment(1.0);
  scale_adjustment[0][0] = scale;
  scale_adjustment[1][1] = scale;
  scale_adjustment[3][0] = scale - 1.f;
  scale_adjustment[3][1] = scale - 1.f;

  return scale_adjustment * camera.projection();
}

ModelDisplayListBuilder::~ModelDisplayListBuilder() = default;

ModelDisplayListBuilder::ModelDisplayListBuilder(
    vk::Device device, const Stage& stage, const Model& model,
    const Camera& camera, float scale, const TexturePtr& white_texture,
    const TexturePtr& shadow_texture, const mat4& shadow_matrix,
    vec3 ambient_light_intensity, vec3 direct_light_intensity,
    ModelData* model_data, ModelRenderer* renderer,
    ModelRenderPassPtr render_pass, ModelDisplayListFlags flags)
    : device_(device),
      volume_(stage.viewing_volume()),
      view_transform_(camera.transform()),
      projection_transform_(AdjustCameraTransform(stage, camera, scale)),
      use_material_textures_(render_pass->UseMaterialTextures()),
      disable_depth_test_(flags & ModelDisplayListFlag::kDisableDepthTest),
      white_texture_(white_texture),
      shadow_texture_(shadow_texture ? shadow_texture : white_texture),
      shadow_matrix_(shadow_matrix),
      renderer_(renderer),
      render_pass_(std::move(render_pass)),
      pipeline_cache_(render_pass_->pipeline_cache()),
      uniform_buffer_pool_(model_data->uniform_buffer_pool()),
      per_model_descriptor_set_pool_(
          model_data->per_model_descriptor_set_pool()),
      per_object_descriptor_set_pool_(
          model_data->per_object_descriptor_set_pool()) {
  FXL_DCHECK(white_texture_);

  // Obtain a uniform buffer and write the PerModel data to it.
  PrepareUniformBufferForWriteOfSize(sizeof(ModelData::PerModel),
                                     kMinUniformBufferOffsetAlignment);
  auto per_model = reinterpret_cast<ModelData::PerModel*>(
      &(uniform_buffer_->ptr()[uniform_buffer_write_index_]));
  per_model->frag_coord_to_uv_multiplier =
      vec2(1.f / volume_.width(), 1.f / volume_.height());
  per_model->ambient_light_intensity = ambient_light_intensity;
  per_model->direct_light_intensity = direct_light_intensity;
  per_model->time = model.time();

  if (shadow_texture) {
    textures_.push_back(shadow_texture);
    per_model->shadow_map_uv_multiplier =
        vec2(1.f / shadow_texture->width(), 1.f / shadow_texture->height());
  }

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
  buffer_info.buffer = uniform_buffer_->vk();
  buffer_info.range = sizeof(ModelData::PerModel);
  buffer_info.offset = uniform_buffer_write_index_;
  buffer_write.pBufferInfo = &buffer_info;

  auto& image_write = writes[1];
  image_write.dstSet = per_model_descriptor_set_;
  image_write.dstBinding = ModelData::PerModel::kDescriptorSetSamplerBinding;
  image_write.dstArrayElement = 0;
  image_write.descriptorCount = 1;
  image_write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  vk::DescriptorImageInfo image_info;
  image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  image_info.imageView = shadow_texture_->vk_image_view();
  image_info.sampler = shadow_texture_->vk_sampler();
  image_write.pImageInfo = &image_info;

  uniform_buffer_write_index_ += sizeof(ModelData::PerModel);

  BufferPtr vp_uniform_buffer;
  uint32_t vp_uniform_buffer_offset = 0;
  if (camera.pose_buffer()) {
    // If the camera has a pose buffer bind the output of the late latching
    // shader to the VP uniform
    vp_uniform_buffer = camera.latched_pose_buffer();
    // Pose buffer latching shader writes the latched pose before the VP matrix
    // so we need to offset past it.
    vp_uniform_buffer_offset = sizeof(hmd::Pose);
  } else {
    // If the camera does not have a pose buffer obtain a uniform buffer
    // in the usual way and write the ViewProjection data into it directly.
    PrepareUniformBufferForWriteOfSize(sizeof(ModelData::ViewProjection),
                                       kMinUniformBufferOffsetAlignment);
    auto view_projection = reinterpret_cast<ModelData::ViewProjection*>(
        &(uniform_buffer_->ptr()[uniform_buffer_write_index_]));
    view_projection->vp_matrix = projection_transform_ * view_transform_;
    vp_uniform_buffer = uniform_buffer_;
    vp_uniform_buffer_offset = uniform_buffer_write_index_;
  }

  auto& vp_buffer_write = writes[2];
  vp_buffer_write.dstSet = per_model_descriptor_set_;
  vp_buffer_write.dstBinding =
      ModelData::ViewProjection::kDescriptorSetUniformBinding;
  vp_buffer_write.dstArrayElement = 0;
  vp_buffer_write.descriptorCount = 1;
  vp_buffer_write.descriptorType = vk::DescriptorType::eUniformBuffer;
  vk::DescriptorBufferInfo vp_buffer_info;
  vp_buffer_info.buffer = vp_uniform_buffer->vk();
  vp_buffer_info.range = sizeof(ModelData::ViewProjection);
  vp_buffer_info.offset = vp_uniform_buffer_offset;
  vp_buffer_write.pBufferInfo = &vp_buffer_info;

  uniform_buffer_write_index_ += sizeof(ModelData::ViewProjection);

  device_.updateDescriptorSets(ModelData::PerModel::kDescriptorCount, writes, 0,
                               nullptr);
}

void ModelDisplayListBuilder::AddClipperObject(const Object& object) {
  if (object.shape().type() == Shape::Type::kNone) {
    // The object has no shape to clip against.
    return;
  }

  FXL_DCHECK(object.shape().modifiers() == ShapeModifiers());

  PrepareUniformBufferForWriteOfSize(sizeof(ModelData::PerObject),
                                     kMinUniformBufferOffsetAlignment);
  vk::DescriptorSet descriptor_set = ObtainPerObjectDescriptorSet();
  UpdateDescriptorSetForObject(object, descriptor_set);

  ModelDisplayList::Item item;
  item.descriptor_set = descriptor_set;
  item.mesh = renderer_->GetMeshForShape(object.shape());
  pipeline_spec_.mesh_spec = item.mesh->spec();
  pipeline_spec_.shape_modifiers = object.shape().modifiers();
  pipeline_spec_.is_clippee = clip_depth_ > 0;
  pipeline_spec_.clipper_state =
      ModelPipelineSpec::ClipperState::kBeginClipChildren;
  if (object.material()) {
    pipeline_spec_.has_material = true;
    pipeline_spec_.is_opaque = object.material()->opaque();
  } else {
    pipeline_spec_.has_material = false;
    pipeline_spec_.is_opaque = false;
  }
  item.pipeline = pipeline_cache_->GetPipeline(pipeline_spec_);
  item.stencil_reference = clip_depth_;

  items_.push_back(item);
}

void ModelDisplayListBuilder::AddClipperAndClippeeObjects(
    const Object& object) {
  const bool is_clippee = clip_depth_ > 0;

  // Remember the beginning and end of clipper-items, so that we can later
  // undo their effects upon the stencil buffer.
  size_t clipper_start_index = items_.size();

  // Drawing clippers will increment the values in the stencil buffer.  Update
  // |clip_depth_| so that children can test against the correct value.
  AddClipperObject(object);
  for (auto& clipper : object.clippers()) {
    FXL_DCHECK(clipper.clippers().empty());
    FXL_DCHECK(clipper.clippees().empty());
    AddClipperObject(clipper);
  }
  // Remember the beginning and end of clipper-items, so that we can later
  // undo their effects upon the stencil buffer.
  size_t clipper_end_index = items_.size();

  ++clip_depth_;

  // Recursively draw clipped children, z-sorting the semitransparent ones.
  // TODO(rosswang): See TODOs in |ModelRenderer|.
  const std::vector<Object>& clippees = object.clippees();
  std::vector<uint32_t> alpha_children;
  for (size_t i = 0; i < clippees.size(); i++) {
    const Object& o = clippees[i];
    if (!o.material() || o.material()->opaque()) {
      AddObject(o);
    } else {
      alpha_children.push_back(i);
    }
  }

  ZSort(&alpha_children, clippees, view_transform_ * projection_transform_);

  for (uint32_t i : alpha_children) {
    AddObject(clippees[i]);
  }

  // Revert the stencil buffer to the previous state.
  // TODO: if we knew that no subsequent children were to be clipped, we
  // could avoid this.
  for (size_t index = clipper_start_index; index < clipper_end_index; ++index) {
    ModelDisplayList::Item item = items_[index];
    pipeline_spec_.mesh_spec = item.mesh->spec();
    pipeline_spec_.shape_modifiers = ShapeModifiers();
    pipeline_spec_.is_clippee = is_clippee;
    pipeline_spec_.clipper_state =
        ModelPipelineSpec::ClipperState::kEndClipChildren;
    // Even if the object has a material, we already drew it the first time;
    // now we just need to clear the stencil buffer.
    pipeline_spec_.has_material = false;
    pipeline_spec_.is_opaque = false;
    pipeline_spec_.disable_depth_test = disable_depth_test_;
    item.pipeline = pipeline_cache_->GetPipeline(pipeline_spec_);
    item.stencil_reference = clip_depth_;

    items_.push_back(std::move(item));
  }

  --clip_depth_;
}

void ModelDisplayListBuilder::AddNonClipperObject(const Object& object) {
  FXL_DCHECK(object.clippees().empty());
  if (object.material()) {
    // Simply push the item.
    PrepareUniformBufferForWriteOfSize(sizeof(ModelData::PerObject),
                                       kMinUniformBufferOffsetAlignment);
    vk::DescriptorSet descriptor_set = ObtainPerObjectDescriptorSet();
    UpdateDescriptorSetForObject(object, descriptor_set);

    ModelDisplayList::Item item;
    item.descriptor_set = descriptor_set;
    item.mesh = renderer_->GetMeshForShape(object.shape());
    pipeline_spec_.mesh_spec = item.mesh->spec();
    pipeline_spec_.shape_modifiers = object.shape().modifiers();
    pipeline_spec_.is_clippee = clip_depth_ > 0;
    pipeline_spec_.clipper_state =
        ModelPipelineSpec::ClipperState::kNoClipChildren;
    pipeline_spec_.has_material = true;
    pipeline_spec_.is_opaque = object.material()->opaque();
    pipeline_spec_.disable_depth_test = disable_depth_test_;
    item.pipeline = pipeline_cache_->GetPipeline(pipeline_spec_);
    item.stencil_reference = clip_depth_;

    items_.push_back(std::move(item));
  }
}

void ModelDisplayListBuilder::AddObject(const Object& object) {
  const bool has_clippees = !object.clippees().empty();

  if (has_clippees) {
    AddClipperAndClippeeObjects(object);
  } else {
    // Some of these may need to be drawn (i.e. if they have both shape and
    // material), even though there are no clippees to clip.  In this case,
    // draw them without updating the stencil buffer.
    AddNonClipperObject(object);
    for (auto& clipper : object.clippers()) {
      AddNonClipperObject(clipper);
    }
  }
}

void ModelDisplayListBuilder::UpdateDescriptorSetForObject(
    const Object& object, vk::DescriptorSet descriptor_set) {
  auto per_object = reinterpret_cast<ModelData::PerObject*>(
      &(uniform_buffer_->ptr()[uniform_buffer_write_index_]));
  *per_object = ModelData::PerObject();  // initialize with default values

  auto& mat = object.material();

  // Push uniforms for scale/translation and color.
  per_object->model_transform = object.transform();
  per_object->shadow_transform = shadow_matrix_ * object.transform();
  per_object->color =
      mat ? mat->color() : vec4(1, 1, 1, 1);  // opaque by default

  // Find the texture to use, either the object's material's texture, or
  // the default texture if the material doesn't have one.
  vk::ImageView image_view;
  vk::Sampler sampler;
  if (auto& texture = mat ? mat->texture() : nullptr) {
    if (!use_material_textures_) {
      // The object's material has a texture, but we choose not to use it.
      image_view = white_texture_->vk_image_view();
      sampler = white_texture_->vk_sampler();
    } else {
      image_view = object.material()->vk_image_view();
      sampler = object.material()->vk_sampler();
      textures_.push_back(texture);
    }
  } else {
    // No texture available.  Use white texture, so that object's color shows.
    image_view = white_texture_->vk_image_view();
    sampler = white_texture_->vk_sampler();
  }

  // TODO: Remove when WobbleModifierAbsorber is stable.
  if (object.shape().modifiers() & ShapeModifier::kWobble) {
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
    buffer_info.buffer = uniform_buffer_->vk();
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
    barrier.buffer = uniform_buffer->vk();
    barrier.offset = 0;
    barrier.size = uniform_buffer->size();

    command_buffer->vk().pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,
        vk::PipelineStageFlagBits::eVertexShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        vk::DependencyFlags(), 0, nullptr, 1, &barrier, 0, nullptr);

    // The display list still needs to retain the buffer, and since it no
    // longer needs special treatment, add it in with the other resources.
    //
    // NOTE: see comment in ModelData constructor... at some point there will
    // probably be an Escher-wide UniformBufferPool that will use a per-Frame
    // resource management strategy.  ModelDisplayListBuilder will not be around
    // for long enough to make updating it worthwhile.
    resources_.push_back(std::move(uniform_buffer));
  }
  uniform_buffers_.clear();

  auto display_list = fxl::MakeRefCounted<ModelDisplayList>(
      renderer_->resource_recycler(), per_model_descriptor_set_,
      std::move(items_), std::move(textures_), std::move(resources_));
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
    size_t size, size_t alignment) {
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
