// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_renderer.h"

#include <glm/gtc/matrix_access.hpp>
#include <vulkan/vulkan.hpp>

// TODO(ES-153): possibly delete.  See comment below about NUM_CLIP_PLANES.
#include "src/ui/lib/escher/debug/debug_font.h"
#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/paper/paper_render_queue_context.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/paper/paper_shader_structs.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/scene/object.h"
// TODO(fxbug.dev/44894): try to avoid including an "impl" file.
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer_pipeline_state.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/util/string_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"
#include "src/ui/lib/escher/vk/pipeline_builder.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

PaperRendererPtr PaperRenderer::New(EscherWeakPtr escher, const PaperRendererConfig& config) {
  return fxl::AdoptRef(new PaperRenderer(std::move(escher), config));
}

PaperRenderer::PaperRenderer(EscherWeakPtr weak_escher, const PaperRendererConfig& config)
    : escher_(weak_escher),
      context_(weak_escher->vulkan_context()),
      config_(config),
      draw_call_factory_(weak_escher, config),
      shape_cache_(std::move(weak_escher), config),
      // TODO(ES-151): (probably) move programs into PaperDrawCallFactory.
      ambient_light_program_(escher()->GetProgram(kAmbientLightProgramData)),
      no_lighting_program_(escher()->GetProgram(kNoLightingProgramData)),
      point_light_program_(escher()->GetProgram(kPointLightProgramData)),
      point_light_falloff_program_(escher()->GetProgram(kPointLightFalloffProgramData)),
      shadow_volume_geometry_program_(escher()->GetProgram(kShadowVolumeGeometryProgramData)),
      shadow_volume_geometry_debug_program_(
          escher()->GetProgram(kShadowVolumeGeometryDebugProgramData)) {
  FX_DCHECK(config.num_depth_buffers > 0);
  depth_buffers_.resize(config.num_depth_buffers);
  msaa_buffers_.resize(config.num_depth_buffers);
}

PaperRenderer::~PaperRenderer() = default;

PaperRenderer::FrameData::FrameData(const FramePtr& frame_in,
                                    std::shared_ptr<BatchGpuUploader> gpu_uploader_in,
                                    const PaperScenePtr& scene_in, const ImagePtr& output_image_in,
                                    std::pair<TexturePtr, TexturePtr> depth_and_msaa_textures,
                                    const std::vector<Camera>& cameras_in)
    : frame(frame_in),
      output_image(output_image_in),
      depth_texture(depth_and_msaa_textures.first),
      msaa_texture(depth_and_msaa_textures.second),
      gpu_uploader(gpu_uploader_in),
      scene(scene_in) {
  // Scale the camera viewports to pixel coordinates in the output framebuffer.
  for (auto& cam : cameras_in) {
    vk::Rect2D rect = cam.viewport().vk_rect_2d(output_image->width(), output_image->height());
    vk::Viewport viewport(rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height, 0,
                          1);

    UniformBinding binding;
    CameraEye eye = CameraEye::kLeft;
    if (auto buffer = cam.latched_pose_buffer()) {
      // The camera has a latched pose-buffer, so we use it to obtain a
      // view-projection matrix in the shader.  We pass the eye_index as a
      // push-constant to obtain the correct matrix.
      frame->cmds()->KeepAlive(buffer);
      binding.descriptor_set_index = PaperShaderLatchedPoseBuffer::kDescriptorSet;
      binding.binding_index = PaperShaderLatchedPoseBuffer::kDescriptorBinding;
      binding.buffer = buffer.get();
      binding.offset = 0;
      binding.size = sizeof(PaperShaderLatchedPoseBuffer);
      eye = cam.latched_camera_eye();
    } else {
      // The camera has no latched pose-buffer, so allocate/populate uniform
      // data with the same layout, based on the camera's projection/transform
      // matrices.
      auto pair = NewPaperShaderUniformBinding<PaperShaderLatchedPoseBuffer>(frame);
      pair.first->vp_matrix[0] = cam.projection() * cam.transform();
      pair.first->vp_matrix[1] = cam.projection() * cam.transform();
      binding = pair.second;
    }

    cameras.push_back({.binding = binding,
                       .rect = rect,
                       .viewport = viewport,
                       .eye_index = (eye == CameraEye::kLeft ? 0U : 1U)});
  }

  // Generate a UniformBinding for global scene data (e.g. ambient lighting).
  {
    auto writable_binding = NewPaperShaderUniformBinding<PaperShaderSceneData>(frame);
    writable_binding.first->ambient_light_color = scene->ambient_light.color;
    scene_uniform_bindings.push_back(writable_binding.second);
  }

  // Generate a UniformBinding containing data for all point lights, if any.
  auto num_lights = scene->num_point_lights();
  if (num_lights > 0) {
    auto writable_binding = NewPaperShaderUniformBinding<PaperShaderPointLight>(frame, num_lights);
    auto* point_lights = writable_binding.first;
    for (size_t i = 0; i < num_lights; ++i) {
      const PaperPointLight& light = scene->point_lights[i];
      point_lights[i].position = vec4(light.position, 1);
      point_lights[i].color = vec4(light.color, 1);
      point_lights[i].falloff = light.falloff;
    }
    scene_uniform_bindings.push_back(writable_binding.second);
  }
}

PaperRenderer::FrameData::~FrameData() = default;

void PaperRenderer::SetConfig(const PaperRendererConfig& config) {
  FX_DCHECK(!frame_data_) << "Illegal call to SetConfig() during a frame.";
  FX_DCHECK(SupportsShadowType(config.shadow_type))
      << "Unsupported shadow type: " << config.shadow_type;
  FX_DCHECK(config.num_depth_buffers > 0);
  FX_DCHECK(config.msaa_sample_count == 1 || config.msaa_sample_count == 2 ||
            config.msaa_sample_count == 4);

  const auto& supported_sample_counts = escher()->device()->caps().msaa_sample_counts;
  if (supported_sample_counts.find(config.msaa_sample_count) == supported_sample_counts.end()) {
    FX_LOGS(ERROR) << "PaperRenderer: MSAA sample count ("
                   << static_cast<uint32_t>(config.msaa_sample_count)
                   << ") is not supported on this device. SetConfig failed.";
    return;
  }
  if (escher()
          ->device()
          ->caps()
          .GetMatchingDepthStencilFormat({config.depth_stencil_format})
          .result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "PaperRenderer: Depth stencil format ("
                   << vk::to_string(config.depth_stencil_format)
                   << ") is not supported on this device. SetConfig failed.";
    return;
  }

  if (config.msaa_sample_count != config_.msaa_sample_count) {
    FX_VLOGS(1) << "PaperRenderer: MSAA sample count set to: " << config.msaa_sample_count
                << " (was: " << config_.msaa_sample_count << ")";
    depth_buffers_.clear();
    msaa_buffers_.clear();
  }

  if (config.depth_stencil_format != config_.depth_stencil_format) {
    FX_VLOGS(1) << "PaperRenderer: depth_stencil_format set to: "
                << vk::to_string(config.depth_stencil_format)
                << " (was: " << vk::to_string(config_.depth_stencil_format) << ")";
    depth_buffers_.clear();
  }

  if (config.num_depth_buffers != config_.num_depth_buffers) {
    FX_VLOGS(1) << "PaperRenderer: num_depth_buffers set to: " << config.num_depth_buffers
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

bool PaperRenderer::SupportsShadowType(PaperRendererShadowType shadow_type) const {
  return shadow_type == PaperRendererShadowType::kNone ||
         shadow_type == PaperRendererShadowType::kShadowVolume;
}

void PaperRenderer::BeginFrame(const FramePtr& frame, std::shared_ptr<BatchGpuUploader> uploader,
                               const PaperScenePtr& scene, const std::vector<Camera>& cameras,
                               const ImagePtr& output_image) {
  TRACE_DURATION("gfx", "PaperRenderer::BeginFrame");
  FX_DCHECK(!frame_data_) << "already in a frame.";
  FX_DCHECK(frame && uploader && scene && !cameras.empty() && output_image);

  auto index = frame->frame_number() % depth_buffers_.size();
  TexturePtr& depth_texture = depth_buffers_[index];
  TexturePtr& msaa_texture = msaa_buffers_[index];
  RenderFuncs::ObtainDepthAndMsaaTextures(escher(), frame, output_image->info(),
                                          config_.msaa_sample_count, config_.depth_stencil_format,
                                          depth_texture, msaa_texture);

  frame_data_ = std::make_unique<FrameData>(
      frame, std::move(uploader), scene, output_image,
      std::pair<TexturePtr, TexturePtr>(depth_texture, msaa_texture), cameras);

  shape_cache_.BeginFrame(frame_data_->gpu_uploader.get(), frame->frame_number());

  {
    // As described in the header file, we use the first camera's transform for
    // the purpose of depth-sorting.
    mat4 camera_transform = cameras[0].transform();

    // A camera's transform doesn't move the camera; it is applied to the rest
    // of the scene to "move it away from the camera".  Therefore, the camera's
    // position in the scene can be obtained by inverting it and applying it to
    // the origin, or equivalently by inverting the transform and taking the
    // rightmost (translation) column.
    vec3 camera_pos(glm::column(glm::inverse(camera_transform), 3));

    // The camera points down the negative-Z axis, so its world-space direction
    // can be obtained by applying the camera transform to the direction vector
    // [0, 0, -1, 0] (remembering that directions vectors have a w-coord of 0,
    // vs. 1 for position vectors).  This is equivalent to taking the negated
    // third column of the transform.
    vec3 camera_dir = -vec3(glm::column(camera_transform, 2));

    draw_call_factory_.BeginFrame(frame, frame_data_->gpu_uploader.get(), scene.get(),
                                  &transform_stack_, &render_queue_, &shape_cache_, camera_pos,
                                  camera_dir);
  }
}

void PaperRenderer::FinalizeFrame() {
  TRACE_DURATION("gfx", "PaperRenderer::FinalizeFrame");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized && frame_data_->gpu_uploader);

  // We may need to lazily instantiate |debug_font|, or delete it. If the former, this needs to be
  // done before we submit the GPU uploader's tasks.

  // TODO(ES-224): Clean up lazy instantiation. Right now, DebugFont and DebugRects are
  // created/destroyed from frame-to-frame.
  if (config_.debug_frame_number) {
    DrawDebugText(std::to_string(frame_data_->frame->frame_number()), {10, 10}, 4);
  }
  if (!frame_data_->texts.empty()) {
    if (!debug_font_) {
      debug_font_ = DebugFont::New(frame_data_->gpu_uploader.get(), escher()->image_cache());
    }
  } else {
    debug_font_.reset();
  }

  if (!frame_data_->lines.empty()) {
    if (!debug_lines_) {
      debug_lines_ = DebugRects::New(frame_data_->gpu_uploader.get(), escher()->image_cache());
    }
  } else {
    debug_lines_.reset();
  }

  // At this point, all uploads are finished, and no Vulkan commands that depend on these
  // uploads have yet been generated.  After this point, no additional uploads are allowed.
  frame_data_->scene_finalized = true;
  frame_data_->gpu_uploader.reset();
}

void PaperRenderer::EndFrame(const std::vector<SemaphorePtr>& upload_wait_semaphores) {
  TRACE_DURATION("gfx", "PaperRenderer::EndFrame");
  FX_DCHECK(frame_data_);
  FX_DCHECK(frame_data_->scene_finalized && !frame_data_->gpu_uploader);

  for (const SemaphorePtr& upload_wait_semaphore : upload_wait_semaphores) {
    frame_data_->frame->cmds()->AddWaitSemaphore(
        std::move(upload_wait_semaphore), vk::PipelineStageFlagBits::eVertexInput |
                                              vk::PipelineStageFlagBits::eFragmentShader |
                                              vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                              vk::PipelineStageFlagBits::eTransfer);
  }

  // Generate the Vulkan commands to render the frame.
  render_queue_.Sort();
  {
    for (uint32_t camera_index = 0; camera_index < frame_data_->cameras.size(); ++camera_index) {
      switch (config_.shadow_type) {
        case PaperRendererShadowType::kNone:
          GenerateCommandsForNoShadows(camera_index);
          break;
        case PaperRendererShadowType::kShadowVolume:
          GenerateCommandsForShadowVolumes(camera_index);
          break;
        default:
          FX_DCHECK(false) << "Unsupported shadow type: " << config_.shadow_type;
          GenerateCommandsForNoShadows(camera_index);
      }
    }
  }
  render_queue_.Clear();

  GenerateDebugCommands(frame_data_->frame->cmds());

  frame_data_ = nullptr;
  transform_stack_.Clear();
  shape_cache_.EndFrame();
  draw_call_factory_.EndFrame();
}

void PaperRenderer::DrawDebugText(std::string text, vk::Offset2D offset, int32_t scale) {
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  // TODO(ES-245): Add error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  frame_data_->texts.push_back({text, offset, scale});
}

void PaperRenderer::DrawVLine(escher::DebugRects::Color kColor, uint32_t x_coord, int32_t y_start,
                              uint32_t y_end, uint32_t thickness) {
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  vk::Rect2D rect;
  vk::Offset2D offset = {static_cast<int32_t>(x_coord), y_start};
  vk::Extent2D extent = {static_cast<uint32_t>(x_coord + thickness), y_end};

  // Adds error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  FX_DCHECK(extent.width >= 0 && extent.width < frame_data_->output_image->width());
  FX_DCHECK(extent.height >= 0 && extent.height < frame_data_->output_image->height());

  rect.offset = offset;
  rect.extent = extent;

  frame_data_->lines.push_back({kColor, rect});
}

void PaperRenderer::DrawHLine(escher::DebugRects::Color kColor, int32_t y_coord, int32_t x_start,
                              uint32_t x_end, int32_t thickness) {
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  vk::Rect2D rect;
  vk::Offset2D offset = {x_start, static_cast<int32_t>(y_coord)};
  vk::Extent2D extent = {x_end, static_cast<uint32_t>(y_coord + thickness)};

  // Adds error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  FX_DCHECK(extent.width >= 0 && extent.width < frame_data_->output_image->width());
  FX_DCHECK(extent.height >= 0 && extent.height < frame_data_->output_image->height());

  rect.offset = offset;
  rect.extent = extent;

  frame_data_->lines.push_back({kColor, rect});
}

void PaperRenderer::BindSceneAndCameraUniforms(uint32_t camera_index) {
  auto* cmd_buf = frame_data_->frame->cmds();
  for (UniformBinding& binding : frame_data_->scene_uniform_bindings) {
    binding.Bind(cmd_buf);
  }
  frame_data_->cameras[camera_index].binding.Bind(cmd_buf);
}

bool PaperRenderer::SupportsMaterial(const PaperMaterialPtr& material) {
  if (!material) {
    return false;
  }
  if (material->type() == Material::Type::kWireframe && !escher()->supports_wireframe()) {
    FX_LOGS(ERROR) << "Device doesn't support feature fillModeNonSolid. "
                      "Draw Calls will not be enqueued.";
    return false;
  }
  return true;
}

void PaperRenderer::Draw(PaperDrawable* drawable, PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::Draw");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  // For restoring state afterward.
  size_t transform_stack_size = transform_stack_.size();
  size_t num_clip_planes = transform_stack_.num_clip_planes();
  drawable->DrawInScene(frame_data_->scene.get(), &draw_call_factory_, &transform_stack_,
                        frame_data_->frame.get(), flags);
  transform_stack_.Clear({transform_stack_size, num_clip_planes});
}

void PaperRenderer::DrawCircle(float radius, const PaperMaterialPtr& material,
                               PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawCircle");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  FX_DCHECK(material);
  if (!SupportsMaterial(material)) {
    return;
  }
  draw_call_factory_.DrawCircle(radius, *material.get(), flags);
}

void PaperRenderer::DrawRect(vec2 min, vec2 max, const PaperMaterialPtr& material,
                             PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawRect");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  FX_DCHECK(material);
  if (!SupportsMaterial(material)) {
    return;
  }
  draw_call_factory_.DrawRect(min, max, *material.get(), flags);
}

// Convenience wrapper around the standard DrawRect function.
void PaperRenderer::DrawRect(float width, float height, const PaperMaterialPtr& material,
                             PaperDrawableFlags flags) {
  const vec2 extent(width, height);
  DrawRect(-0.5f * extent, 0.5f * extent, material, flags);
}

void PaperRenderer::DrawRoundedRect(const RoundedRectSpec& spec, const PaperMaterialPtr& material,
                                    PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawRoundedRect");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  FX_DCHECK(material);
  if (!SupportsMaterial(material)) {
    return;
  }
  draw_call_factory_.DrawRoundedRect(spec, *material.get(), flags);
}

void PaperRenderer::DrawBoundingBox(const BoundingBox& box, const PaperMaterialPtr& material,
                                    PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawBoundingBox");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  FX_DCHECK(material);
  if (!SupportsMaterial(material)) {
    return;
  }
  if (material->texture()) {
    FX_LOGS(ERROR) << "TODO(ES-218): Box meshes do not currently support textures.";
    return;
  }

  mat4 matrix = box.CreateTransform();
  transform_stack_.PushTransform(matrix);
  draw_call_factory_.DrawBoundingBox(*material.get(), flags);
  transform_stack_.Pop();
}

void PaperRenderer::DrawMesh(const MeshPtr& mesh, const PaperMaterialPtr& material,
                             PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawMesh");
  FX_DCHECK(frame_data_);
  FX_DCHECK(!frame_data_->scene_finalized);

  FX_DCHECK(material);
  if (!SupportsMaterial(material)) {
    return;
  }
  draw_call_factory_.DrawMesh(mesh, *material.get(), flags);
}

// TODO(ES-154): in "no shadows" mode, should we:
// - not use the other lights, and boost the ambient intensity?
// - still use the lights, allowing a BRDF, distance-based-falloff etc.
// The right answer is probably to separate the shadow algorithm from the
// lighting model.
void PaperRenderer::GenerateCommandsForNoShadows(uint32_t camera_index) {
  TRACE_DURATION("gfx", "PaperRenderer::GenerateCommandsForNoShadows");

  const FramePtr& frame = frame_data_->frame;
  CommandBuffer* cmd_buf = frame->cmds();

  RenderPassInfo render_pass_info;
  FX_DCHECK(camera_index < frame_data_->cameras.size());
  auto render_area = frame_data_->cameras[camera_index].rect;

  if (!RenderPassInfo::InitRenderPassInfo(&render_pass_info, render_area, frame_data_->output_image,
                                          frame_data_->depth_texture, frame_data_->msaa_texture,
                                          escher()->image_view_allocator())) {
    FX_LOGS(ERROR) << "PaperRenderer::GenerateCommandsForNoShadows(): "
                      "RenderPassInfo initialization failed. Exiting.";
    return;
  }

  cmd_buf->BeginRenderPass(render_pass_info);
  frame->AddTimestamp("started no-shadows render pass");

  BindSceneAndCameraUniforms(camera_index);

  const CameraData& cam_data = frame_data_->cameras[camera_index];
  cmd_buf->SetViewport(cam_data.viewport);
  cmd_buf->PushConstants(PaperShaderPushConstants{
      .light_index = 0,  // ignored
      .eye_index = cam_data.eye_index,
  });

  {
    PaperRenderQueueContext context;
    context.set_draw_mode(PaperRendererDrawMode::kAmbient);

    // Render wireframe.
    context.set_shader_program(no_lighting_program_);
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kWireframe);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kWireframe);

    // Render opaque.
    context.set_shader_program(ambient_light_program_);
    cmd_buf->SetWireframe(false);
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);

    // Render translucent.
    context.set_shader_program(no_lighting_program_);
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kTranslucent);
  }
  cmd_buf->EndRenderPass();
  frame->AddTimestamp("finished no-shadows render pass");
}

void PaperRenderer::GenerateCommandsForShadowVolumes(uint32_t camera_index) {
  TRACE_DURATION("gfx", "PaperRenderer::GenerateCommandsForShadowVolumes");

  const uint32_t width = frame_data_->output_image->width();
  const uint32_t height = frame_data_->output_image->height();
  const FramePtr& frame = frame_data_->frame;
  CommandBuffer* cmd_buf = frame->cmds();

  RenderPassInfo render_pass_info;
  FX_DCHECK(camera_index < frame_data_->cameras.size());
  auto render_area = frame_data_->cameras[camera_index].rect;

  if (!RenderPassInfo::InitRenderPassInfo(&render_pass_info, render_area, frame_data_->output_image,
                                          frame_data_->depth_texture, frame_data_->msaa_texture,
                                          escher()->image_view_allocator())) {
    FX_LOGS(ERROR) << "PaperRenderer::GenerateCommandsForShadowVolumes(): "
                      "RenderPassInfo initialization failed. Exiting.";
    return;
  }

  cmd_buf->BeginRenderPass(render_pass_info);
  frame->AddTimestamp("started shadow_volume render pass");

  BindSceneAndCameraUniforms(camera_index);

  const CameraData& cam_data = frame_data_->cameras[camera_index];
  cmd_buf->SetViewport(cam_data.viewport);

  PaperRenderQueueContext context;

  // Configure the render context for a depth/ambient "pass" (this isn't an
  // actual Vulkan pass/subpass), and emit Vulkan commands into the command
  // buffer.
  {
    cmd_buf->PushConstants(PaperShaderPushConstants{
        .light_index = 0,  // ignored
        .eye_index = cam_data.eye_index,
    });

    context.set_draw_mode(PaperRendererDrawMode::kAmbient);

    // Render wireframe.
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kWireframe);
    context.set_shader_program(no_lighting_program_);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kWireframe);

    // Render opaque.
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    context.set_shader_program(ambient_light_program_);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
  }

  cmd_buf->SetStencilTest(true);
  cmd_buf->SetDepthTestAndWrite(true, false);
  cmd_buf->SetStencilFrontReference(0xff, 0xff, 0U);
  cmd_buf->SetStencilBackReference(0xff, 0xff, 0U);
  cmd_buf->SetBlendFactors(
      /*src_color_blend=*/vk::BlendFactor::eOne, /*src_alpha_blend=*/vk::BlendFactor::eZero,
      /*dst_color_blend=*/vk::BlendFactor::eOne, /*dst_alpha_blend=*/vk::BlendFactor::eOne);
  cmd_buf->SetBlendOp(vk::BlendOp::eAdd);

  // For each point light, emit Vulkan commands first to draw the stencil shadow
  // geometry for that light, and then to add the lighting contribution for that
  // light.
  const uint32_t num_point_lights = frame_data_->scene->num_point_lights();
  for (uint32_t i = 0; i < num_point_lights; ++i) {
    // Some setup doesn't need to be done for the first light.
    if (i != 0) {
      // Must clear the stencil buffer for every light except the first one.
      cmd_buf->ClearDepthStencilAttachmentRect(cam_data.rect.offset, cam_data.rect.extent,
                                               render_pass_info.clear_depth_stencil,
                                               vk::ImageAspectFlagBits::eStencil);

      // Ensure that each light starts with blending disabled.  Otherwise, the 2nd and subsequent
      // lights would use a different pipeline for |shadow_volume_geometry_program_|.
      cmd_buf->SetBlendEnable(false);

      if (config_.debug) {
        // Replace values set by the debug visualization.
        cmd_buf->SetStencilTest(true);
        cmd_buf->SetWireframe(false);
      }
    }
    cmd_buf->PushConstants(PaperShaderPushConstants{
        .light_index = i,
        .eye_index = cam_data.eye_index,
    });

    // Emit commands for stencil shadow geometry.
    {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeGeometry);
      context.set_shader_program(shadow_volume_geometry_program_);

      // Draw front and back faces of the shadow volumes in a single pass.  We
      // use the standard approach of modifying the stencil buffer only when the
      // depth test is passed, incrementing the stencil value for front-faces
      // and decrementing it for back-faces.
      cmd_buf->SetCullMode(vk::CullModeFlagBits::eNone);
      cmd_buf->SetStencilFrontOps(vk::CompareOp::eAlways, vk::StencilOp::eIncrementAndWrap,
                                  vk::StencilOp::eKeep, vk::StencilOp::eKeep);
      cmd_buf->SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eDecrementAndWrap,
                                 vk::StencilOp::eKeep, vk::StencilOp::eKeep);

      // Leaving this as eLessOrEqual would result in total self-shadowing by
      // all shadow-casters.
      cmd_buf->SetDepthCompareOp(vk::CompareOp::eLess);

      render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
    }

    // Emit commands for adding lighting contribution.
    {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeLighting);

      // Use a slightly less expensive shader when distance-based attenuation is
      // disabled.
      const bool use_light_falloff = frame_data_->scene->point_lights[i].falloff > 0.f;
      if (use_light_falloff) {
        context.set_shader_program(point_light_falloff_program_);
      } else {
        context.set_shader_program(point_light_program_);
      }

      cmd_buf->SetBlendEnable(true);

      cmd_buf->SetCullMode(vk::CullModeFlagBits::eBack);
      cmd_buf->SetDepthCompareOp(vk::CompareOp::eLessOrEqual);

      cmd_buf->SetStencilFrontOps(vk::CompareOp::eEqual, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                  vk::StencilOp::eKeep);
      cmd_buf->SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                 vk::StencilOp::eKeep);

      render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
    }

    if (config_.debug) {
      if (!escher_->supports_wireframe()) {
        FX_LOGS(WARNING) << "Wireframe not supported; cannot visualize shadow volume geometry.";
      } else {
        context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeGeometry);
        context.set_shader_program(shadow_volume_geometry_debug_program_);

        cmd_buf->SetBlendEnable(false);
        cmd_buf->SetStencilTest(false);
        cmd_buf->SetWireframe(true);
        cmd_buf->SetCullMode(vk::CullModeFlagBits::eNone);

        render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
      }
    }
  }

  // Draw translucent geometry without lighting.
  context.set_draw_mode(PaperRendererDrawMode::kAmbient);
  context.set_shader_program(no_lighting_program_);
  cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
  render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kTranslucent);

  cmd_buf->EndRenderPass();
  frame->AddTimestamp("finished shadow_volume render pass");
}

void PaperRenderer::GenerateDebugCommands(CommandBuffer* cmd_buf) {
  TRACE_DURATION("gfx", "PaperRenderer::GenerateDebugCommands");

  // Exit early if there is no debug rendering to be done.
  if (frame_data_->texts.size() == 0 && frame_data_->lines.size() == 0) {
    return;
  }

  const FramePtr& frame = frame_data_->frame;
  frame->AddTimestamp("started debug render pass");

  auto& output_image = frame_data_->output_image;
  auto swapchain_layout = output_image->swapchain_layout();

  if (swapchain_layout == vk::ImageLayout::eUndefined) {
    FX_LOGS(ERROR) << "PaperRenderer::GenerateDebugCommands(): "
                      "exiting due to undefined swapchain layout.";
    return;
  }

  if (output_image->layout() != swapchain_layout) {
    FX_LOGS(ERROR) << "PaperRenderer::GeneratedDebugCommands(): "
                      "Layout of output_image is not initialized to swapchain layout. Exiting.";
    return;
  }

  cmd_buf->ImageBarrier(
      output_image, swapchain_layout, vk::ImageLayout::eTransferDstOptimal,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
      vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

  {
    TRACE_DURATION("gfx", "PaperRenderer::GenerateDebugCommands[text]");
    for (std::size_t i = 0; i < frame_data_->texts.size(); i++) {
      const TextData& td = frame_data_->texts[i];
      debug_font_->Blit(cmd_buf, td.text, output_image, td.offset, td.scale);
    }
  }

  {
    TRACE_DURATION("gfx", "PaperRenderer::GenerateDebugCommands[lines]");
    for (std::size_t i = 0; i < frame_data_->lines.size(); i++) {
      const LineData& ld = frame_data_->lines[i];
      debug_lines_->Blit(cmd_buf, ld.kColor, output_image, ld.rect);
    }
  }

  cmd_buf->ImageBarrier(
      output_image, vk::ImageLayout::eTransferDstOptimal, swapchain_layout,
      vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite);

  frame->AddTimestamp("finished debug render pass");
}

// Helper for WarmPipelineAndRenderPassCaches().  Return the render-pass that should be used for
// pipeline creation for the specified config.
static impl::RenderPassPtr WarmRenderPassCache(impl::RenderPassCache* cache,
                                               const PaperRendererConfig& config,
                                               vk::Format output_format,
                                               vk::ImageLayout output_swapchain_layout) {
  TRACE_DURATION("gfx", "PaperRenderer::WarmRenderPassCache", "format",
                 vk::to_string(output_format), "layout", vk::to_string(output_swapchain_layout));
  RenderPassInfo info;

  RenderPassInfo::AttachmentInfo color_attachment_info;
  color_attachment_info.format = output_format;
  color_attachment_info.swapchain_layout = output_swapchain_layout;
  color_attachment_info.sample_count = 1;

  if (!RenderPassInfo::InitRenderPassInfo(&info, color_attachment_info, config.depth_stencil_format,
                                          output_format, config.msaa_sample_count, false)) {
    FX_LOGS(ERROR) << "WarmRenderPassCache(): InitRenderPassInfo failed. Exiting.";
    return nullptr;
  };

  return cache->ObtainRenderPass(info, /*allow_render_pass_creation*/ true);
}

// Helper for WarmPipelineAndRenderPassCaches.
static void BindMeshSpecHelper(CommandBufferPipelineState* cbps, const MeshSpec& mesh_spec) {
  const uint32_t total_attribute_count = mesh_spec.total_attribute_count();
  BlockAllocator allocator(512);
  RenderFuncs::VertexAttributeBinding* attribute_bindings =
      RenderFuncs::NewVertexAttributeBindings(PaperRenderFuncs::kMeshAttributeBindingLocations,
                                              &allocator, mesh_spec, total_attribute_count);

  for (uint32_t i = 0; i < total_attribute_count; ++i) {
    attribute_bindings[i].Bind(cbps);
  }

  // NOTE: we don't actually have a buffer to bind, nor an offset into the bound buffer.  This would
  // be a problem if we tried to generate a draw cmd, but is OK because we just need the stride and
  // input-rate in order to pre-generate pipelines.
  for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
    cbps->BindVertices(i, vk::Buffer(), 0, mesh_spec.stride(i), vk::VertexInputRate::eVertex);
  }
}

// Helper for WarmPipelineAndRenderPassCaches.
static void WarmProgramHelper(impl::PipelineLayoutCache* pipeline_layout_cache,
                              const ShaderProgramPtr& program, CommandBufferPipelineState* cbps,
                              const std::vector<SamplerPtr>& immutable_samplers) {
  TRACE_DURATION("gfx", "PaperRenderer::WarmProgramHelper");

  // Generate pipeline which doesn't require an immutable sampler.
  PipelineLayoutPtr layout = program->ObtainPipelineLayout(pipeline_layout_cache, nullptr);
  cbps->FlushGraphicsPipeline(layout.get(), program.get());

  // Generate pipelines which require immutable samplers.
  for (auto& sampler : immutable_samplers) {
    PipelineLayoutPtr layout = program->ObtainPipelineLayout(pipeline_layout_cache, sampler);
    cbps->FlushGraphicsPipeline(layout.get(), program.get());
  }
}

// Populate caches with all render passes and pipelines required by |config|.
void PaperRenderer::WarmPipelineAndRenderPassCaches(
    Escher* escher, const PaperRendererConfig& config, vk::Format output_format,
    vk::ImageLayout output_swapchain_layout, const std::vector<SamplerPtr>& immutable_samplers) {
  TRACE_DURATION("gfx", "PaperRenderer::WarmPipelineAndRenderPassCaches");

  CommandBufferPipelineState cbps(escher->pipeline_builder()->GetWeakPtr());

  // Obtain and set the render pass; this is the only render pass that is used, so we just need to
  // set it once.
  // TODO(fxbug.dev/44894): try to avoid using this "impl" type directly.
  impl::RenderPassPtr render_pass = WarmRenderPassCache(escher->render_pass_cache(), config,
                                                        output_format, output_swapchain_layout);

  FX_DCHECK(render_pass);
  cbps.set_render_pass(render_pass.get());

  // Set up vertex buffer bindings, as well as bindings to attributes within those buffers.  Of
  // course we don't actually have buffers right now; that's OK... see comments in the helper func
  // for details.
  {
    TRACE_DURATION("gfx", "PaperRenderer::WarmPipelineAndRenderPassCaches[bind mesh spec]");
    BindMeshSpecHelper(&cbps, PaperShapeCache::kShadowVolumeMeshSpec());
  }
  // NOTE: different mesh specs are used depending on whether stencil shadows
  // are enabled.  But it doesn't matter, because CommandBuffer will only use whichever attributes
  // are required for the specified shader.
  // TODO(fxbug.dev/44898): once kShadowVolumeMeshSpec and kStandardMeshSpec are constexpr, we
  // should be able to use static_assert() here.
  FX_DCHECK(PaperShapeCache::kShadowVolumeMeshSpec().attributes[0] ==
            PaperShapeCache::kStandardMeshSpec().attributes[0]);
  FX_DCHECK(PaperShapeCache::kShadowVolumeMeshSpec().attributes[1] ==
            PaperShapeCache::kStandardMeshSpec().attributes[1]);

  switch (config.shadow_type) {
    case PaperRendererShadowType::kNone: {
      if (escher->supports_wireframe()) {
        cbps.SetToDefaultState(CommandBuffer::DefaultState::kWireframe);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kNoLightingProgramData), &cbps, immutable_samplers);
      }

      cbps.SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
      WarmProgramHelper(escher->pipeline_layout_cache(),
                        escher->GetProgram(kAmbientLightProgramData), &cbps, immutable_samplers);

      cbps.SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
      WarmProgramHelper(escher->pipeline_layout_cache(), escher->GetProgram(kNoLightingProgramData),
                        &cbps, immutable_samplers);
    } break;
    case PaperRendererShadowType::kShadowVolume: {
      // Wireframe shapes (not shadow volumes).
      if (escher->supports_wireframe()) {
        cbps.SetToDefaultState(CommandBuffer::DefaultState::kWireframe);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kNoLightingProgramData), &cbps, immutable_samplers);
      }

      // Ambient opaque.
      {
        cbps.SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kAmbientLightProgramData), &cbps, immutable_samplers);
      }

      // Set state common to both stencil shadow "geometry" and "lighting" passes.
      cbps.SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
      cbps.SetStencilTest(true);
      cbps.SetDepthTestAndWrite(true, false);
      cbps.SetBlendFactors(
          /*src_color_blend=*/vk::BlendFactor::eOne, /*src_alpha_blend=*/vk::BlendFactor::eZero,
          /*dst_color_blend=*/vk::BlendFactor::eOne, /*dst_alpha_blend=*/vk::BlendFactor::eOne);
      cbps.SetBlendOp(vk::BlendOp::eAdd);

      // Stencil shadow geometry.
      {
        cbps.SetCullMode(vk::CullModeFlagBits::eNone);
        cbps.SetDepthCompareOp(vk::CompareOp::eLess);
        cbps.SetStencilFrontOps(vk::CompareOp::eAlways, vk::StencilOp::eIncrementAndWrap,
                                vk::StencilOp::eKeep, vk::StencilOp::eKeep);
        cbps.SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eDecrementAndWrap,
                               vk::StencilOp::eKeep, vk::StencilOp::eKeep);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kShadowVolumeGeometryProgramData), &cbps,
                          immutable_samplers);
      }

      // Stencil shadow lighting.
      {
        cbps.SetBlendEnable(true);
        cbps.SetCullMode(vk::CullModeFlagBits::eBack);
        cbps.SetDepthCompareOp(vk::CompareOp::eLessOrEqual);
        cbps.SetStencilFrontOps(vk::CompareOp::eEqual, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                vk::StencilOp::eKeep);
        cbps.SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                               vk::StencilOp::eKeep);

        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kPointLightProgramData), &cbps, immutable_samplers);

        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kPointLightFalloffProgramData), &cbps,
                          immutable_samplers);
      }

      // Wireframe shadow volumes (for debug-mode).
      if (escher->supports_wireframe()) {
        cbps.SetBlendEnable(false);
        cbps.SetStencilTest(false);
        cbps.SetWireframe(true);
        cbps.SetCullMode(vk::CullModeFlagBits::eNone);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kShadowVolumeGeometryDebugProgramData), &cbps,
                          immutable_samplers);
      }

      // Translucent.
      {
        cbps.SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
        WarmProgramHelper(escher->pipeline_layout_cache(),
                          escher->GetProgram(kNoLightingProgramData), &cbps, immutable_samplers);
      }

    } break;
    case PaperRendererShadowType::kEnumCount:
    default:
      FX_CHECK(false) << "unhandled shadow type";
  }
}

}  // namespace escher
