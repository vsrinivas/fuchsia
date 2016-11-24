// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/model_renderer.h"

#include "escher/geometry/tessellation.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/image_cache.h"
#include "escher/impl/mesh_impl.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_pipeline.h"
#include "escher/impl/model_pipeline_cache.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image.h"
#include "escher/scene/model.h"
#include "escher/scene/shape.h"
#include "escher/scene/stage.h"
#include "escher/util/image_loader.h"

namespace escher {
namespace impl {

ModelRenderer::ModelRenderer(EscherImpl* escher,
                             ModelData* model_data,
                             vk::Format color_format,
                             vk::Format depth_format)
    : device_(escher->vulkan_context().device),
      mesh_manager_(escher->mesh_manager()),
      model_data_(model_data) {
  rectangle_ = CreateRectangle();
  circle_ = CreateCircle();
  white_texture_ = CreateWhiteTexture(escher);

  CreateRenderPasses(color_format, depth_format);
  pipeline_cache_ = std::make_unique<impl::ModelPipelineCacheOLD>(
      device_, depth_prepass_, lighting_pass_, model_data_);
}

ModelRenderer::~ModelRenderer() {
  device_.destroyRenderPass(depth_prepass_);
  device_.destroyRenderPass(lighting_pass_);
}

void ModelRenderer::Draw(Stage& stage,
                         Model& model,
                         CommandBuffer* command_buffer,
                         const TexturePtr& illumination_texture) {
  vk::CommandBuffer vk_command_buffer = command_buffer->get();

  auto& volume = stage.viewing_volume();

  vk::Viewport viewport;
  viewport.width = volume.width();
  viewport.height = volume.height();
  // TODO: replace these with values from ViewingVolume.  Need to also
  // consider how this works with hacked Z-value in vertex shader (i.e. unhack
  // that at the same time).
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;
  vk_command_buffer.setViewport(0, 1, &viewport);

  auto& objects = model.objects();

  ModelUniformWriter writer(objects.size(), model_data_->device(),
                            model_data_->uniform_buffer_pool(),
                            model_data_->per_model_descriptor_set_pool(),
                            model_data_->per_object_descriptor_set_pool());

  // TODO: temporary hack... this is a way to allow objects to be drawn with
  // color only... if the object's material doesn't have a texture, then this
  // 1-pixel pure-white texture is used.
  vk::ImageView default_image_view = white_texture_->image_view();
  vk::Sampler default_sampler = white_texture_->sampler();

  ModelData::PerModel per_model;
  per_model.frag_coord_to_uv_multiplier =
      vec2(1.f / volume.width(), 1.f / volume.height());
  per_model.time = model.time();
  if (illumination_texture) {
    FTL_DCHECK(!hack_use_depth_prepass);
    writer.WritePerModelData(per_model, illumination_texture->image_view(),
                             illumination_texture->sampler());
  } else {
    FTL_DCHECK(hack_use_depth_prepass);
    writer.WritePerModelData(per_model, default_image_view, default_sampler);
  }

  // Write per-object uniforms, and collect a list of bindings that can be
  // used once the uniforms have been flushed to the GPU.
  FTL_DCHECK(per_object_bindings_.empty());
  {
    const float half_width_recip = 2.f / volume.width();
    const float half_height_recip = 2.f / volume.height();
    const float depth_range_recip = 1.f / volume.depth_range();

    ModelData::PerObject per_object;
    auto& scale_x = per_object.transform[0][0];
    auto& scale_y = per_object.transform[1][1];
    auto& translate_x = per_object.transform[3][0];
    auto& translate_y = per_object.transform[3][1];
    auto& translate_z = per_object.transform[3][2];
    auto& color = per_object.color;
    for (const Object& o : objects) {
      // Push uniforms for scale/translation and color.
      scale_x = o.width() * half_width_recip;
      scale_y = o.height() * half_height_recip;
      translate_x = o.position().x * half_width_recip - 1.f;
      translate_y = o.position().y * half_height_recip - 1.f;
      // Convert "height above the stage" into "distance from the camera",
      // normalized to the range (0,1).  This is passed unaltered through the
      // vertex shader.
      translate_z = 1.f - (volume.far() + o.position().z * depth_range_recip);
      color = vec4(o.material()->color(), 1.f);  // always opaque

      // Find the texture to use, either the object's material's texture, or
      // the default texture if the material doesn't have one.
      vk::ImageView image_view;
      vk::Sampler sampler;
      if (auto& texture = o.material()->texture()) {
        image_view = o.material()->image_view();
        sampler = o.material()->sampler();
        command_buffer->AddUsedResource(texture);
        // TODO: it would be nice if Resource::TakeWaitSemaphore() were virtual
        // so that we could say texture->TakeWaitSemaphore(), instead of needing
        // to know that the image is really the thing that we might need to wait
        // for.  Another approach would be for the Texture constructor to say
        // SetWaitSemaphore(image->TakeWaitSemaphore()), but this isn't a
        // bulletproof solution... what if someone else made a Texture with the
        // same image, and used that one first.  Of course, in general we want
        // lighter-weight synchronization such as events or barriers... need to
        // revisit this whole topic.
        command_buffer->AddWaitSemaphore(
            texture->image()->TakeWaitSemaphore(),
            vk::PipelineStageFlagBits::eFragmentShader);
      } else {
        image_view = default_image_view;
        sampler = default_sampler;
      }

      if (o.shape().modifiers() | ShapeModifier::kWobble) {
        auto wobble = o.shape_modifier_data<ModifierWobble>();
        per_object.wobble = wobble ? *wobble : ModifierWobble();
      }

      per_object_bindings_.push_back(
          writer.WritePerObjectData(per_object, image_view, sampler));
    }
    writer.Flush(command_buffer);
  }

  // Do a second pass over the data, so that flushing the uniform writer above
  // can set a memory barrier before shaders use those uniforms.  This only
  // sucks a litte bit, because we'll eventually want to sort draw calls by
  // pipeline/opacity/depth-order/etc.
  // TODO: sort draw calls.
  ModelPipelineSpec previous_pipeline_spec;
  ModelPipeline* pipeline = nullptr;
  for (size_t i = 0; i < objects.size(); ++i) {
    const Object& o = objects[i];
    const MeshPtr& mesh = GetMeshForShape(o.shape());
    FTL_DCHECK(mesh);

    ModelPipelineSpec pipeline_spec;
    pipeline_spec.mesh_spec = mesh->spec;
    pipeline_spec.shape_modifiers = o.shape().modifiers();
    pipeline_spec.use_depth_prepass = hack_use_depth_prepass;

    // Don't rebind pipeline state if it is already up-to-date.
    if (previous_pipeline_spec != pipeline_spec) {
      pipeline = pipeline_cache_->GetPipeline(
          pipeline_spec, mesh_manager_->GetMeshSpecImpl(mesh->spec));

      vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     pipeline->pipeline());

      // TODO: many pipeline will share the same layout so rebinding may not be
      // necessary.
      writer.BindPerModelData(pipeline->pipeline_layout(), vk_command_buffer);

      previous_pipeline_spec = pipeline_spec;
    }

    // Bind the descriptor set, using the binding obtained in the first pass.
    writer.BindPerObjectData(per_object_bindings_[i],
                             pipeline->pipeline_layout(), vk_command_buffer);

    command_buffer->DrawMesh(mesh);
  }

  per_object_bindings_.clear();
}

const MeshPtr& ModelRenderer::GetMeshForShape(const Shape& shape) const {
  switch (shape.type()) {
    case Shape::Type::kRect:
      return rectangle_;
    case Shape::Type::kCircle:
      return circle_;
    case Shape::Type::kMesh:
      return shape.mesh();
  }
  FTL_CHECK(false);
  return shape.mesh();  // this would DCHECK
}

MeshPtr ModelRenderer::CreateRectangle() {
  return NewSimpleRectangleMesh(mesh_manager_);
}

MeshPtr ModelRenderer::CreateCircle() {
  MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kUV};

  return NewCircleMesh(mesh_manager_, spec, 4, vec2(0.5f, 0.5f), 0.5f);
}

TexturePtr ModelRenderer::CreateWhiteTexture(EscherImpl* escher) {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;

  auto image = escher->image_cache()->NewRgbaImage(1, 1, channels);
  return ftl::MakeRefCounted<Texture>(
      std::move(image), escher->vulkan_context().device, vk::Filter::eNearest);
}

void ModelRenderer::CreateRenderPasses(vk::Format color_format,
                                       vk::Format depth_format) {
  constexpr uint32_t kAttachmentCount = 2;
  const uint32_t kColorAttachment = 0;
  const uint32_t kDepthAttachment = 1;
  vk::AttachmentDescription attachments[kAttachmentCount];
  auto& color_attachment = attachments[kColorAttachment];
  auto& depth_attachment = attachments[kDepthAttachment];

  color_attachment.format = color_format;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  // Load/store ops and image layouts differ between passes; see below.

  depth_attachment.format = depth_format;
  depth_attachment.samples = vk::SampleCountFlagBits::e1;
  depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  // Load/store ops and image layouts differ between passes; see below.

  vk::AttachmentReference color_reference;
  color_reference.attachment = kColorAttachment;
  color_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference depth_reference;
  depth_reference.attachment = kDepthAttachment;
  depth_reference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // Every vk::RenderPass needs at least one subpass.
  vk::SubpassDescription subpass;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.pDepthStencilAttachment = &depth_reference;
  subpass.inputAttachmentCount = 0;  // no other subpasses to sample from

  // Even though we have a single subpass, we need to declare dependencies to
  // support the layout transitions specified by the attachment references.
  constexpr uint32_t kDependencyCount = 2;
  vk::SubpassDependency dependencies[kDependencyCount];
  auto& input_dependency = dependencies[0];
  auto& output_dependency = dependencies[1];

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  input_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp ?!?
  input_dependency.dstSubpass = 0;
  input_dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  input_dependency.dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  input_dependency.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                   vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency.srcSubpass = 0;  // our sole subpass
  output_dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency.srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
  output_dependency.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  output_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // We're almost ready to create the render-passes... we just need to fill in
  // some final values that differ between the passes.
  vk::RenderPassCreateInfo info;
  info.attachmentCount = kAttachmentCount;
  info.pAttachments = attachments;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = kDependencyCount;
  info.pDependencies = dependencies;

  // Create the depth-prepass RenderPass.
  color_attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
  color_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  color_attachment.initialLayout = vk::ImageLayout::eUndefined;
  color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  depth_attachment.initialLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depth_attachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depth_prepass_ = ESCHER_CHECKED_VK_RESULT(device_.createRenderPass(info));

  // Create the illumination RenderPass.
  color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
  color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
  depth_attachment.loadOp = vk::AttachmentLoadOp::eLoad;
  // TODO: Investigate whether we would say eDontCare about the depth_attachment
  // contents.  Ideally the Vulkan implementation would notice that the pipeline
  // that we use during the illumination pass don't write to the depth-buffer
  // (only read), but who knows?  By saying eDontCare, we be indicating that
  // Vulkan wouldn't go out of its way to trash memory that it being used
  // read-only.  Anyway, until profiling indicates that it's a problem, we can
  // leave it this way.
  depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  depth_attachment.initialLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depth_attachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  lighting_pass_ = ESCHER_CHECKED_VK_RESULT(device_.createRenderPass(info));
}

}  // namespace impl
}  // namespace escher
