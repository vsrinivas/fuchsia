// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_renderer2.h"

#include <glm/gtc/matrix_access.hpp>

// TODO(ES-153): possibly delete.  See comment below about NUM_CLIP_PLANES.
#include "lib/escher/geometry/clip_planes.h"

#include "lib/escher/escher.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/paper/paper_legacy_drawable.h"
#include "lib/escher/paper/paper_render_queue_context.h"
#include "lib/escher/paper/paper_scene.h"
#include "lib/escher/paper/paper_shader_structs.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/scene/object.h"
#include "lib/escher/util/string_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/command_buffer.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/render_pass_info.h"
#include "lib/escher/vk/shader_program.h"
#include "lib/escher/vk/texture.h"

namespace escher {

PaperRenderer2Ptr PaperRenderer2::New(EscherWeakPtr escher,
                                      const PaperRendererConfig& config) {
  return fxl::AdoptRef(new PaperRenderer2(std::move(escher), config));
}

PaperRenderer2::PaperRenderer2(EscherWeakPtr weak_escher,
                               const PaperRendererConfig& config)
    : Renderer(weak_escher),
      config_(config),
      draw_call_factory_(weak_escher, config),
      shape_cache_(std::move(weak_escher), config),
      // TODO(ES-151): (probably) move programs into PaperDrawCallFactory.
      ambient_light_program_(escher()->GetGraphicsProgram(
          "shaders/model_renderer/main.vert",
          "shaders/paper/frag/main_ambient_light.frag",
          ShaderVariantArgs({
              {"USE_ATTRIBUTE_UV", "1"},
              // TODO(ES-153): currently required by main.vert.
              {"NO_SHADOW_LIGHTING_PASS", "1"},
          }))),
      point_light_program_(escher()->GetGraphicsProgram(
          "shaders/model_renderer/main.vert",
          "shaders/paper/frag/main_point_light.frag",
          ShaderVariantArgs({
              {"USE_ATTRIBUTE_UV", "1"},
              {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
              {"SHADOW_VOLUME_POINT_LIGHTING", "1"},
          }))),
      point_light_falloff_program_(escher()->GetGraphicsProgram(
          "shaders/model_renderer/main.vert",
          "shaders/paper/frag/main_point_light.frag",
          ShaderVariantArgs({
              {"USE_ATTRIBUTE_UV", "1"},
              {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
              {"USE_PAPER_SHADER_POINT_LIGHT_FALLOFF", "1"},
              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
              {"SHADOW_VOLUME_POINT_LIGHTING", "1"},
          }))),
      shadow_volume_geometry_program_(escher()->GetGraphicsProgram(
          "shaders/model_renderer/main.vert", "",
          ShaderVariantArgs({
              {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
              {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
              {"SHADOW_VOLUME_EXTRUSION", "1"},
          }))),
      shadow_volume_geometry_debug_program_(escher()->GetGraphicsProgram(
          "shaders/model_renderer/main.vert",
          "shaders/model_renderer/main.frag",
          ShaderVariantArgs({
              {"USE_ATTRIBUTE_BLEND_WEIGHT_1", "1"},
              {"USE_PAPER_SHADER_POINT_LIGHT", "1"},
              {"USE_PAPER_SHADER_PUSH_CONSTANTS", "1"},
              {"SHADOW_VOLUME_EXTRUSION", "1"},
          }))) {
  FXL_DCHECK(config.num_depth_buffers > 0);
  depth_buffers_.resize(config.num_depth_buffers);
  msaa_buffers_.resize(config.num_depth_buffers);
}

PaperRenderer2::~PaperRenderer2() { escher()->Cleanup(); }

PaperRenderer2::FrameData::FrameData(
    const FramePtr& frame_in, const PaperScenePtr& scene_in,
    const ImagePtr& output_image_in,
    std::pair<TexturePtr, TexturePtr> depth_and_msaa_textures)
    : frame(frame_in),
      scene(scene_in),
      output_image(output_image_in),
      depth_texture(std::move(depth_and_msaa_textures.first)),
      msaa_texture(std::move(depth_and_msaa_textures.second)),
      gpu_uploader(BatchGpuUploader::New(frame->escher()->GetWeakPtr())) {}

PaperRenderer2::FrameData::~FrameData() = default;

void PaperRenderer2::SetConfig(const PaperRendererConfig& config) {
  FXL_DCHECK(!frame_data_) << "Illegal call to SetConfig() during a frame.";
  FXL_DCHECK(SupportsShadowType(config.shadow_type))
      << "Unsupported shadow type: " << config.shadow_type;
  FXL_DCHECK(config.num_depth_buffers > 0);
  FXL_DCHECK(config.msaa_sample_count == 1 || config.msaa_sample_count == 2 ||
             config.msaa_sample_count == 4);

  if (config.msaa_sample_count != config_.msaa_sample_count) {
    FXL_VLOG(1) << "PaperRenderer2: MSAA sample count set to: "
                << config.msaa_sample_count
                << " (was: " << config_.msaa_sample_count << ")";
    depth_buffers_.clear();
    msaa_buffers_.clear();
  }

  if (config.num_depth_buffers != config_.num_depth_buffers) {
    FXL_VLOG(1) << "PaperRenderer2: num_depth_buffers set to: "
                << config.num_depth_buffers
                << " (was: " << config_.num_depth_buffers << ")";
  }
  // This is done here (instead of the if-statement above) because there may
  // have been a change to the MSAA sample count.
  depth_buffers_.resize(config.num_depth_buffers);
  msaa_buffers_.resize(config.num_depth_buffers);

  config_ = config;

  draw_call_factory_.SetConfig(config_);
  shape_cache_.SetConfig(config_);
}

bool PaperRenderer2::SupportsShadowType(
    PaperRendererShadowType shadow_type) const {
  return shadow_type == PaperRendererShadowType::kNone ||
         shadow_type == PaperRendererShadowType::kShadowVolume;
}

void PaperRenderer2::BeginFrame(const FramePtr& frame,
                                const PaperScenePtr& scene,
                                const Camera& camera,
                                const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "PaperRenderer2::BeginFrame");
  FXL_DCHECK(!frame_data_);

  frame_data_ = std::make_unique<FrameData>(
      frame, scene, output_image,
      ObtainDepthAndMsaaTextures(frame, output_image->info()));

  frame->command_buffer()->TakeWaitSemaphore(
      output_image, vk::PipelineStageFlagBits::eColorAttachmentOutput);
  shape_cache_.BeginFrame(frame_data_->gpu_uploader.get(),
                          frame->frame_number());

  frame_data_->scene_uniform_bindings =
      draw_call_factory_.BeginFrame(frame, scene.get(), &transform_stack_,
                                    &render_queue_, &shape_cache_, camera);
}

void PaperRenderer2::EndFrame() {
  TRACE_DURATION("gfx", "PaperRenderer2::EndFrame");
  FXL_DCHECK(frame_data_);

  frame_data_->gpu_uploader->Submit(SemaphorePtr());

  render_queue_.Sort();
  {
    auto* cmd_buf = frame_data_->frame->cmds();
    for (UniformBinding& binding : frame_data_->scene_uniform_bindings) {
      binding.Bind(cmd_buf);
    }

    switch (config_.shadow_type) {
      case PaperRendererShadowType::kNone:
        GenerateCommandsForNoShadows();
        break;
      case PaperRendererShadowType::kShadowVolume:
        GenerateCommandsForShadowVolumes();
        break;
      default:
        FXL_DCHECK(false) << "Unsupported shadow type: " << config_.shadow_type;
        GenerateCommandsForNoShadows();
    }
  }
  render_queue_.Clear();

  frame_data_ = nullptr;
  transform_stack_.Clear();
  shape_cache_.EndFrame();
  draw_call_factory_.EndFrame();
}

void PaperRenderer2::Draw(PaperDrawable* drawable, PaperDrawableFlags flags,
                          mat4* matrix) {
  TRACE_DURATION("gfx", "PaperRenderer2::Draw");

  // For restoring state afterward.
  size_t transform_stack_size = transform_stack_.size();
  size_t num_clip_planes = transform_stack_.num_clip_planes();

  if (matrix) {
    transform_stack_.PushTransform(*matrix);
  }

  drawable->DrawInScene(frame_data_->scene.get(), &draw_call_factory_,
                        &transform_stack_, frame_data_->frame.get(), flags);

  transform_stack_.Clear({transform_stack_size, num_clip_planes});
}

void PaperRenderer2::DrawCircle(float radius, const PaperMaterialPtr& material,
                                PaperDrawableFlags flags, mat4* matrix) {
  TRACE_DURATION("gfx", "PaperRenderer2::DrawCircle");

  if (!material)
    return;

  if (!matrix) {
    draw_call_factory_.DrawCircle(radius, *material.get(), flags);
  } else {
    // Roll the radius into the transform to avoid an extra push onto the stack;
    // see PaperDrawCallFactory::DrawCircle() for details.
    transform_stack_.PushTransform(
        glm::scale(*matrix, vec3(radius, radius, radius)));
    draw_call_factory_.DrawCircle(1.f, *material.get(), flags);
    transform_stack_.Pop();
  }
}

void PaperRenderer2::DrawRect(vec2 min, vec2 max,
                              const PaperMaterialPtr& material,
                              PaperDrawableFlags flags, mat4* matrix) {
  TRACE_DURATION("gfx", "PaperRenderer2::DrawRect");

  if (!material)
    return;

  if (!matrix) {
    draw_call_factory_.DrawRect(min, max, *material.get(), flags);
  } else {
    transform_stack_.PushTransform(*matrix);
    draw_call_factory_.DrawRect(min, max, *material.get(), flags);
    transform_stack_.Pop();
  }
}

void PaperRenderer2::DrawRoundedRect(const RoundedRectSpec& spec,
                                     const PaperMaterialPtr& material,
                                     PaperDrawableFlags flags, mat4* matrix) {
  TRACE_DURATION("gfx", "PaperRenderer2::DrawRoundedRect");

  if (!material)
    return;

  if (!matrix) {
    draw_call_factory_.DrawRoundedRect(spec, *material.get(), flags);
  } else {
    transform_stack_.PushTransform(*matrix);
    draw_call_factory_.DrawRoundedRect(spec, *material.get(), flags);
    transform_stack_.Pop();
  }
}

void PaperRenderer2::DrawLegacyObject(const Object& obj,
                                      PaperDrawableFlags flags) {
  FXL_DCHECK(frame_data_);

  PaperLegacyDrawable drawable(obj);
  Draw(&drawable, flags);
}

void PaperRenderer2::InitRenderPassInfo(RenderPassInfo* rp) {
  const ImagePtr& output_image = frame_data_->output_image;
  const TexturePtr& depth_texture = frame_data_->depth_texture;
  const TexturePtr& msaa_texture = frame_data_->msaa_texture;

  static constexpr uint32_t kRenderTargetAttachmentIndex = 0;
  static constexpr uint32_t kResolveTargetAttachmentIndex = 1;
  {
    rp->color_attachments[kRenderTargetAttachmentIndex] =
        ImageView::New(escher()->resource_recycler(), output_image);
    rp->num_color_attachments = 1;
    // Clear and store color attachment 0, the sole color attachment.
    rp->clear_attachments = 1u << kRenderTargetAttachmentIndex;
    rp->store_attachments = 1u << kRenderTargetAttachmentIndex;
    // NOTE: we don't need to keep |depth_texture| alive explicitly because it
    // will be kept alive by the render-pass.
    rp->depth_stencil_attachment = depth_texture;
    // Standard flags for a depth-testing render-pass that needs to first clear
    // the depth image.
    rp->op_flags = RenderPassInfo::kClearDepthStencilOp |
                   RenderPassInfo::kOptimalColorLayoutOp |
                   RenderPassInfo::kOptimalDepthStencilLayoutOp;
    rp->clear_color[0].setFloat32({0.1f, 0.1f, 0.2f, 1.f});

    // If MSAA is enabled, we need to explicitly specify the sub-pass in order
    // to specify the resolve attachment.  Otherwise we allow a default subclass
    // to be created.
    if (msaa_texture) {
      FXL_DCHECK(rp->num_color_attachments == 1 && rp->clear_attachments == 1u);
      // Move the output image to attachment #1, so that attachment #0 is always
      // the attachment that we render into.
      rp->color_attachments[kResolveTargetAttachmentIndex] =
          std::move(rp->color_attachments[kRenderTargetAttachmentIndex]);
      rp->color_attachments[kRenderTargetAttachmentIndex] = msaa_texture;
      rp->num_color_attachments = 2;

      // Now that the output image is attachment #1, that's the one we need to
      // store.
      rp->store_attachments = 1u << kResolveTargetAttachmentIndex;

      rp->subpasses.push_back(RenderPassInfo::Subpass{
          .num_color_attachments = 1,
          .color_attachments = {kRenderTargetAttachmentIndex},
          .num_input_attachments = 0,
          .input_attachments = {},
          .num_resolve_attachments = 1,
          .resolve_attachments = {kResolveTargetAttachmentIndex},
      });
    }
  }
  FXL_DCHECK(rp->Validate());
}

// TODO(ES-154): in "no shadows" mode, should we:
// - not use the other lights, and boost the ambient intensity?
// - still use the lights, allowing a BRDF, distance-based-falloff etc.
// The right answer is probably to separate the shadow algorithm from the
// lighting model.
void PaperRenderer2::GenerateCommandsForNoShadows() {
  TRACE_DURATION("gfx", "PaperRenderer2::GenerateCommandsForNoShadows");
  const FramePtr& frame = frame_data_->frame;
  CommandBuffer* cmd_buf = frame->cmds();

  RenderPassInfo render_pass_info;
  InitRenderPassInfo(&render_pass_info);

  cmd_buf->BeginRenderPass(render_pass_info);
  frame->AddTimestamp("started no-shadows render pass");

  {
    PaperRenderQueueContext context;
    context.set_draw_mode(PaperRendererDrawMode::kAmbient);
    context.set_shader_program(ambient_light_program_);

    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    render_queue_.GenerateCommands(cmd_buf, &context,
                                   PaperRenderQueueFlagBits::kOpaque);
  }
  cmd_buf->EndRenderPass();
  frame->AddTimestamp("finished no-shadows render pass");
}

void PaperRenderer2::GenerateCommandsForShadowVolumes() {
  TRACE_DURATION("gfx", "PaperRenderer2::GenerateCommandsForShadowVolumes");
  const uint32_t width = frame_data_->output_image->width();
  const uint32_t height = frame_data_->output_image->height();
  const FramePtr& frame = frame_data_->frame;
  CommandBuffer* cmd_buf = frame->cmds();

  RenderPassInfo render_pass_info;
  InitRenderPassInfo(&render_pass_info);

  cmd_buf->BeginRenderPass(render_pass_info);
  frame->AddTimestamp("started shadow_volume render pass");

  PaperRenderQueueContext context;

  // Configure the render context for a depth/ambient "pass" (this isn't an
  // actual Vulkan pass/subpass), and emit Vulkan commands into the command
  // buffer.
  {
    context.set_draw_mode(PaperRendererDrawMode::kAmbient);
    context.set_shader_program(ambient_light_program_);
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

    render_queue_.GenerateCommands(cmd_buf, &context,
                                   PaperRenderQueueFlagBits::kOpaque);
  }

  cmd_buf->SetStencilTest(true);
  cmd_buf->SetDepthTestAndWrite(true, false);
  cmd_buf->SetStencilFrontReference(0xff, 0xff, 0U);
  cmd_buf->SetStencilBackReference(0xff, 0xff, 0U);

  // For each point light, emit Vulkan commands first to draw the stencil shadow
  // geometry for that light, and then to add the lighting contribution for that
  // light.
  const uint32_t num_point_lights = frame_data_->scene->num_point_lights();
  for (uint32_t i = 0; i < num_point_lights; ++i) {
    // Must clear the stencil buffer for every light except the first one.
    if (i != 0) {
      cmd_buf->ClearDepthStencilAttachmentRect(
          {0, 0}, {width, height}, render_pass_info.clear_depth_stencil,
          vk::ImageAspectFlagBits::eStencil);

      if (config_.debug) {
        // Replace values set by the debug visualization.
        cmd_buf->SetStencilTest(true);
        cmd_buf->SetWireframe(false);
      }
    }
    cmd_buf->PushConstants(PaperShaderPushConstants{.light_index = i});

    // Emit commands for stencil shadow geometry.
    {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeGeometry);
      context.set_shader_program(shadow_volume_geometry_program_);

      // Draw front and back faces of the shadow volumes in a single pass.  We
      // use the standard approach of modifying the stencil buffer only when the
      // depth test is passed, incrementing the stencil value for front-faces
      // and decrementing it for back-faces.
      cmd_buf->SetCullMode(vk::CullModeFlagBits::eNone);
      cmd_buf->SetStencilFrontOps(vk::CompareOp::eAlways,
                                  vk::StencilOp::eIncrementAndWrap,
                                  vk::StencilOp::eKeep, vk::StencilOp::eKeep);
      cmd_buf->SetStencilBackOps(vk::CompareOp::eAlways,
                                 vk::StencilOp::eDecrementAndWrap,
                                 vk::StencilOp::eKeep, vk::StencilOp::eKeep);

      // Leaving this as eLessOrEqual would result in total self-shadowing by
      // all shadow-casters.
      cmd_buf->SetDepthCompareOp(vk::CompareOp::eLess);

      render_queue_.GenerateCommands(cmd_buf, &context,
                                     PaperRenderQueueFlagBits::kOpaque);
    }

    // Emit commands for adding lighting contribution.
    {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeLighting);

      // Use a slightly less expensive shader when distance-based attenuation is
      // disabled.
      const bool use_light_falloff =
          frame_data_->scene->point_lights[i].falloff > 0.f;
      if (use_light_falloff) {
        context.set_shader_program(point_light_falloff_program_);
      } else {
        context.set_shader_program(point_light_program_);
      }

      cmd_buf->SetBlendEnable(true);
      cmd_buf->SetBlendFactors(vk::BlendFactor::eOne, vk::BlendFactor::eZero,
                               vk::BlendFactor::eOne, vk::BlendFactor::eOne);
      cmd_buf->SetBlendOp(vk::BlendOp::eAdd);

      cmd_buf->SetCullMode(vk::CullModeFlagBits::eBack);
      cmd_buf->SetDepthCompareOp(vk::CompareOp::eLessOrEqual);

      cmd_buf->SetStencilFrontOps(vk::CompareOp::eEqual, vk::StencilOp::eKeep,
                                  vk::StencilOp::eKeep, vk::StencilOp::eKeep);
      cmd_buf->SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eKeep,
                                 vk::StencilOp::eKeep, vk::StencilOp::eKeep);

      render_queue_.GenerateCommands(cmd_buf, &context,
                                     PaperRenderQueueFlagBits::kOpaque);
    }

    if (config_.debug) {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeGeometry);
      context.set_shader_program(shadow_volume_geometry_debug_program_);

      cmd_buf->SetBlendEnable(false);
      cmd_buf->SetStencilTest(false);
      cmd_buf->SetWireframe(true);
      cmd_buf->SetCullMode(vk::CullModeFlagBits::eNone);
      render_queue_.GenerateCommands(cmd_buf, &context,
                                     PaperRenderQueueFlagBits::kOpaque);
    }
  }
  cmd_buf->EndRenderPass();
  frame->AddTimestamp("finished shadow_volume render pass");
}

std::pair<TexturePtr, TexturePtr> PaperRenderer2::ObtainDepthAndMsaaTextures(
    const FramePtr& frame, const ImageInfo& info) {
  FXL_DCHECK(!depth_buffers_.empty());

  // Support for other sample_counts should fairly easy to add, if necessary.
  FXL_DCHECK(info.sample_count == 1);

  auto index = frame->frame_number() % depth_buffers_.size();
  TexturePtr& depth_texture = depth_buffers_[index];
  TexturePtr& msaa_texture = msaa_buffers_[index];

  if (!depth_texture || info.width != depth_texture->width() ||
      info.height != depth_texture->height() ||
      config_.msaa_sample_count !=
          depth_texture->image()->info().sample_count) {
    // Need to generate a new depth buffer.
    {
      TRACE_DURATION("gfx",
                     "PaperRenderer2::ObtainDepthAndMsaaTextures (new depth)");
      depth_texture = escher()->NewAttachmentTexture(
          vk::Format::eD24UnormS8Uint, info.width, info.height,
          config_.msaa_sample_count, vk::Filter::eLinear);
    }
    // If the sample count is 1, there is no need for a MSAA buffer.
    if (config_.msaa_sample_count == 1) {
      msaa_texture = nullptr;
    } else {
      TRACE_DURATION("gfx",
                     "PaperRenderer2::ObtainDepthAndMsaaTextures (new msaa)");
      // TODO(SCN-634): use lazy memory allocation and transient attachments
      // when available.
      msaa_texture = escher()->NewAttachmentTexture(
          info.format, info.width, info.height, config_.msaa_sample_count,
          vk::Filter::eLinear
          // TODO(ES-73): , vk::ImageUsageFlagBits::eTransientAttachment);
      );

      frame->cmds()->ImageBarrier(
          msaa_texture->image(), vk::ImageLayout::eUndefined,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::PipelineStageFlagBits::eAllGraphics, vk::AccessFlags(),
          vk::PipelineStageFlagBits::eColorAttachmentOutput,
          vk::AccessFlagBits::eColorAttachmentWrite);
    }
  }
  return {depth_texture, msaa_texture};
}

}  // namespace escher
