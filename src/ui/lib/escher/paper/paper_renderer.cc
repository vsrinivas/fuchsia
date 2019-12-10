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
#include "src/ui/lib/escher/scene/object.h"
#include "src/ui/lib/escher/util/string_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

// Used to calculate the area of the debug graph that bars will be drawn in.
constexpr int32_t kWidthPadding = 150;

PaperRendererPtr PaperRenderer::New(EscherWeakPtr escher, const PaperRendererConfig& config) {
  return fxl::AdoptRef(new PaperRenderer(std::move(escher), config));
}

PaperRenderer::PaperRenderer(EscherWeakPtr weak_escher, const PaperRendererConfig& config)
    : Renderer(weak_escher),
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
  FXL_DCHECK(config.num_depth_buffers > 0);
  depth_buffers_.resize(config.num_depth_buffers);
  msaa_buffers_.resize(config.num_depth_buffers);
}

PaperRenderer::~PaperRenderer() { escher()->Cleanup(); }

PaperRenderer::PaperFrame::PaperFrame(const FramePtr& frame_in,
                                      std::shared_ptr<BatchGpuUploader> gpu_uploader_in,
                                      const PaperScenePtr& scene_in,
                                      const ImagePtr& output_image_in,
                                      std::pair<TexturePtr, TexturePtr> depth_and_msaa_textures,
                                      const std::vector<Camera>& cameras_in)
    : FrameData(frame_in, gpu_uploader_in, output_image_in, depth_and_msaa_textures),
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

PaperRenderer::PaperFrame::~PaperFrame() = default;

void PaperRenderer::SetConfig(const PaperRendererConfig& config) {
  FXL_DCHECK(!frame_data_) << "Illegal call to SetConfig() during a frame.";
  FXL_DCHECK(SupportsShadowType(config.shadow_type))
      << "Unsupported shadow type: " << config.shadow_type;
  FXL_DCHECK(config.num_depth_buffers > 0);
  FXL_DCHECK(config.msaa_sample_count == 1 || config.msaa_sample_count == 2 ||
             config.msaa_sample_count == 4);

  if (config.msaa_sample_count != config_.msaa_sample_count) {
    FXL_VLOG(1) << "PaperRenderer: MSAA sample count set to: " << config.msaa_sample_count
                << " (was: " << config_.msaa_sample_count << ")";
    depth_buffers_.clear();
    msaa_buffers_.clear();
  }

  if (config.depth_stencil_format != config_.depth_stencil_format) {
    FXL_VLOG(1) << "PaperRenderer: depth_stencil_format set to: "
                << vk::to_string(config.depth_stencil_format)
                << " (was: " << vk::to_string(config_.depth_stencil_format) << ")";
    depth_buffers_.clear();
  }

  if (config.num_depth_buffers != config_.num_depth_buffers) {
    FXL_VLOG(1) << "PaperRenderer: num_depth_buffers set to: " << config.num_depth_buffers
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
  FXL_DCHECK(!frame_data_) << "already in a frame.";
  FXL_DCHECK(frame && uploader && scene && !cameras.empty() && output_image);

  frame_data_ = std::make_unique<PaperFrame>(
      frame, std::move(uploader), scene, output_image,
      ObtainDepthAndMsaaTextures(frame, output_image->info(), config_.msaa_sample_count,
                                 config_.depth_stencil_format),
      cameras);

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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized && frame_data_->gpu_uploader);

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

  GraphDebugData();

  // TODO(ES-247): Move graphing out of escher.
  if (!frame_data_->lines.empty() || !debug_times_.empty()) {
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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(frame_data_->scene_finalized && !frame_data_->gpu_uploader);

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
          FXL_DCHECK(false) << "Unsupported shadow type: " << config_.shadow_type;
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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  // TODO(ES-245): Add error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  frame_data_->texts.push_back({text, offset, scale});
}

void PaperRenderer::DrawVLine(escher::DebugRects::Color kColor, uint32_t x_coord, int32_t y_start,
                              uint32_t y_end, uint32_t thickness) {
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  vk::Rect2D rect;
  vk::Offset2D offset = {static_cast<int32_t>(x_coord), y_start};
  vk::Extent2D extent = {static_cast<uint32_t>(x_coord + thickness), y_end};

  // Adds error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  FXL_DCHECK(extent.width >= 0 && extent.width < frame_data_->output_image->width());
  FXL_DCHECK(extent.height >= 0 && extent.height < frame_data_->output_image->height());

  rect.offset = offset;
  rect.extent = extent;

  frame_data_->lines.push_back({kColor, rect});
}

void PaperRenderer::DrawHLine(escher::DebugRects::Color kColor, int32_t y_coord, int32_t x_start,
                              uint32_t x_end, int32_t thickness) {
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  vk::Rect2D rect;
  vk::Offset2D offset = {x_start, static_cast<int32_t>(y_coord)};
  vk::Extent2D extent = {x_end, static_cast<uint32_t>(y_coord + thickness)};

  // Adds error checking to make sure math will not cause negative
  // values or the bars to go off screen.
  FXL_DCHECK(extent.width >= 0 && extent.width < frame_data_->output_image->width());
  FXL_DCHECK(extent.height >= 0 && extent.height < frame_data_->output_image->height());

  rect.offset = offset;
  rect.extent = extent;

  frame_data_->lines.push_back({kColor, rect});
}

void PaperRenderer::DrawDebugGraph(std::string x_label, std::string y_label,
                                   DebugRects::Color lineColor) {
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  const int32_t frame_width = frame_data_->output_image->width();
  const int32_t frame_height = frame_data_->output_image->height();

  const uint16_t x_axis = frame_width - kWidthPadding;
  const uint16_t y_axis = frame_height - kHeightPadding;
  const uint16_t h_interval = (y_axis - kHeightPadding) / 35;

  DrawVLine(lineColor, kWidthPadding, kHeightPadding, y_axis, 10);  // Vertical Line (x-axis).
  DrawHLine(lineColor, y_axis, kWidthPadding, x_axis, 10);          // Horizontal line (y_axis).

  DrawDebugText(x_label, {5, frame_height / 2}, 5);
  DrawDebugText(y_label, {frame_width / 2, frame_height - (kHeightPadding - 25)}, 5);

  // Colored bar used to show acceptable vs concerning values (acceptable below bar).
  DrawHLine(escher::DebugRects::kGreen, y_axis - (h_interval * 16), kWidthPadding + 10, x_axis, 5);
}

void PaperRenderer::AddDebugTimeStamp(TimeStamp ts) { debug_times_.push_back(ts); }

void PaperRenderer::BindSceneAndCameraUniforms(uint32_t camera_index) {
  auto* cmd_buf = frame_data_->frame->cmds();
  for (UniformBinding& binding : frame_data_->scene_uniform_bindings) {
    binding.Bind(cmd_buf);
  }
  frame_data_->cameras[camera_index].binding.Bind(cmd_buf);
}

void PaperRenderer::Draw(PaperDrawable* drawable, PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::Draw");
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  if (!material)
    return;
  draw_call_factory_.DrawCircle(radius, *material.get(), flags);
}

void PaperRenderer::DrawRect(vec2 min, vec2 max, const PaperMaterialPtr& material,
                             PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawRect");
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  if (!material)
    return;
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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  if (!material)
    return;
  draw_call_factory_.DrawRoundedRect(spec, *material.get(), flags);
}

void PaperRenderer::DrawBoundingBox(const BoundingBox& box, const PaperMaterialPtr& material,
                                    PaperDrawableFlags flags) {
  TRACE_DURATION("gfx", "PaperRenderer::DrawBoundingBox");
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  if (!material) {
    return;
  }

  if (material->texture()) {
    FXL_LOG(ERROR) << "TODO(ES-218): Box meshes do not currently support textures.";
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
  FXL_DCHECK(frame_data_);
  FXL_DCHECK(!frame_data_->scene_finalized);

  if (!material) {
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
  FXL_DCHECK(camera_index < frame_data_->cameras.size());
  auto render_area = frame_data_->cameras[camera_index].rect;
  InitRenderPassInfo(&render_pass_info, escher()->image_view_allocator(), *frame_data_.get(),
                     render_area);

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
  FXL_DCHECK(camera_index < frame_data_->cameras.size());
  auto render_area = frame_data_->cameras[camera_index].rect;
  InitRenderPassInfo(&render_pass_info, escher()->image_view_allocator(), *frame_data_.get(),
                     render_area);

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

    context.set_shader_program(no_lighting_program_);
    context.set_draw_mode(PaperRendererDrawMode::kAmbient);

    // Render wireframe.
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kWireframe);
    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kWireframe);

    // Render opaque.
    context.set_shader_program(ambient_light_program_);
    cmd_buf->SetWireframe(false);
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);

    render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
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
      // Must clear the stencil buffer for every light except the first one.
      cmd_buf->ClearDepthStencilAttachmentRect(cam_data.rect.offset, cam_data.rect.extent,
                                               render_pass_info.clear_depth_stencil,
                                               vk::ImageAspectFlagBits::eStencil);

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
      cmd_buf->SetBlendFactors(vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendFactor::eOne,
                               vk::BlendFactor::eOne);
      cmd_buf->SetBlendOp(vk::BlendOp::eAdd);

      cmd_buf->SetCullMode(vk::CullModeFlagBits::eBack);
      cmd_buf->SetDepthCompareOp(vk::CompareOp::eLessOrEqual);

      cmd_buf->SetStencilFrontOps(vk::CompareOp::eEqual, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                  vk::StencilOp::eKeep);
      cmd_buf->SetStencilBackOps(vk::CompareOp::eAlways, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                 vk::StencilOp::eKeep);

      render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
    }

    if (config_.debug) {
      context.set_draw_mode(PaperRendererDrawMode::kShadowVolumeGeometry);
      context.set_shader_program(shadow_volume_geometry_debug_program_);

      cmd_buf->SetBlendEnable(false);
      cmd_buf->SetStencilTest(false);
      cmd_buf->SetWireframe(true);
      cmd_buf->SetCullMode(vk::CullModeFlagBits::eNone);
      render_queue_.GenerateCommands(cmd_buf, &context, PaperRenderQueueFlagBits::kOpaque);
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

void PaperRenderer::GraphDebugData() {
  const int16_t x_start = kWidthPadding + 10;
  const int16_t y_axis = frame_data_->output_image->height() - kHeightPadding;
  const int16_t x_axis = frame_data_->output_image->width() - kWidthPadding;
  const int16_t h_interval = (y_axis - kHeightPadding) / 35;
  const int16_t w_interval = frame_data_->output_image->width() / 100;

  const int16_t middle_bar = y_axis - (h_interval * 16) + 2;

  for (std::size_t i = 0; i < debug_times_.size(); i++) {
    auto t = debug_times_[i];

    int16_t render_time = t.render_done - t.render_start;
    int16_t presentation_time = t.actual_present - t.target_present;

    if (static_cast<int16_t>(x_start + (i * w_interval)) <= x_axis) {
      if (render_time != 0)
        DrawVLine(escher::DebugRects::kRed, x_start + (i * w_interval), y_axis,
                  y_axis - (h_interval * render_time), w_interval);
      if (t.latch_point != 0)
        DrawVLine(escher::DebugRects::kYellow, x_start + (i * w_interval), y_axis,
                  y_axis - (h_interval * t.latch_point), w_interval);
      if (t.update_done != 0)
        DrawVLine(escher::DebugRects::kBlue, x_start + (i * w_interval), y_axis,
                  y_axis - (h_interval * t.update_done), w_interval);
      // if (presentation_time != 0)
      //   DrawVLine(escher::DebugRects::kPurple, x_start + (i * w_interval), middle_bar,
      //             middle_bar - (h_interval * presentation_time), w_interval);
    } else {
      // TODO(ES-246): Delete and replace values in array
    }
  }
}

void PaperRenderer::GenerateDebugCommands(CommandBuffer* cmd_buf) {
  TRACE_DURATION("gfx", "PaperRenderer::GenerateDebugCommands");

  // Exit early if there is no debug rendering to be done.
  if (frame_data_->texts.size() == 0 && frame_data_->lines.size() == 0 &&
      debug_times_.size() == 0) {
    return;
  }

  const FramePtr& frame = frame_data_->frame;
  frame->AddTimestamp("started debug render pass");

  auto& output_image = frame_data_->output_image;
  auto initial_layout = output_image->layout();
  auto target_layout = output_image->swapchain_layout();

  if (target_layout == vk::ImageLayout::eUndefined) {
    FXL_LOG(WARNING) << "PaperRenderer::GenerateDebugCommands(): "
                        "exiting due to undefined swapchain layout.";
    return;
  }

  cmd_buf->ImageBarrier(
      output_image, initial_layout, vk::ImageLayout::eTransferDstOptimal,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
      vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

  for (std::size_t i = 0; i < frame_data_->texts.size(); i++) {
    const TextData& td = frame_data_->texts[i];
    debug_font_->Blit(cmd_buf, td.text, output_image, td.offset, td.scale);
  }

  for (std::size_t i = 0; i < frame_data_->lines.size(); i++) {
    const LineData& ld = frame_data_->lines[i];
    debug_lines_->Blit(cmd_buf, ld.kColor, output_image, ld.rect);
  }

  cmd_buf->ImageBarrier(
      output_image, vk::ImageLayout::eTransferDstOptimal, target_layout,
      vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite,
      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer,
      vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite);

  frame->AddTimestamp("finished debug render pass");
}

}  // namespace escher
