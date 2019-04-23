// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/engine_renderer.h"

#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/engine_renderer_visitor.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/lights/point_light.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/stereo_camera.h"
#include "lib/escher/hmd/pose_buffer_latching_shader.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/paper/paper_scene.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/vk/image.h"

// TODO(SCN-1113): Move this someplace.  PoseBufferLatchingShader assumes this,
// but we can't put it there because it lives in a Zircon-ignorant part of
// Escher.
#include <type_traits>
static_assert(
    std::is_same<zx_time_t, int64_t>::value,
    "PoseBufferLatchingShader incorrectly assumes that zx_time_t is int64_t");

namespace scenic_impl {
namespace gfx {

EngineRenderer::EngineRenderer(escher::EscherWeakPtr weak_escher)
    : escher_(std::move(weak_escher)),
      // We use two depth buffers so that we can render multiple Layers without
      // introducing a GPU stall.
      paper_renderer2_(escher::PaperRenderer2::New(
          escher_, {.shadow_type = escher::PaperRendererShadowType::kNone,
                    .num_depth_buffers = 2})),
      pose_buffer_latching_shader_(
          std::make_unique<escher::hmd::PoseBufferLatchingShader>(escher_)) {
}

EngineRenderer::~EngineRenderer() = default;

void EngineRenderer::RenderLayers(const escher::FramePtr& frame,
                                  zx_time_t target_presentation_time,
                                  const escher::ImagePtr& output_image,
                                  const std::vector<Layer*>& layers) {
  // NOTE: this name is important for benchmarking.  Do not remove or modify it
  // without also updating the "process_gfx_trace.go" script.
  TRACE_DURATION("gfx", "EngineRenderer::RenderLayers");

  // Render each layer, except the bottom one. Create an escher::Object for
  // each layer, which will be composited as part of rendering the final
  // layer.
  // TODO(SCN-1254): the efficiency of this GPU compositing could be
  // improved on tile-based GPUs by generating each layer in a subpass and
  // compositing it into |output_image| in another subpass.  This is currently
  // infeasible because we're rendering some layers with PaperRenderer and
  // others with PaperRenderer2; it will be much easier once PaperRenderer is
  // deleted.
  std::vector<escher::Object> overlay_objects;
  if (layers.size() > 1) {
    overlay_objects.reserve(layers.size() - 1);
    auto it = layers.begin();
    while (++it != layers.end()) {
      auto layer = *it;
      auto texture = escher::Texture::New(
          escher_->resource_recycler(),
          GetLayerFramebufferImage(layer->width(), layer->height()),
          // TODO(SCN-1270): shouldn't need linear filter, since this is
          // 1-1 pixel mapping.  Verify when re-enabling multi-layer support.
          vk::Filter::eLinear);

      DrawLayer(frame, target_presentation_time, layer, texture->image(), {});

      // TODO(SCN-1093): it would be preferable to insert barriers instead of
      // using semaphores.
      auto semaphore = escher::Semaphore::New(escher_->vk_device());
      frame->SubmitPartialFrame(semaphore);
      texture->image()->SetWaitSemaphore(std::move(semaphore));

      auto material = escher::Material::New(layer->color(), std::move(texture));
      material->set_opaque(layer->opaque());

      overlay_objects.push_back(escher::Object::NewRect(
          escher::Transform(layer->translation()), std::move(material)));
    }
  }

  // TODO(SCN-1270): add support for multiple layers.
  if (layers.size() > 1) {
    FXL_LOG(ERROR)
        << "EngineRenderer::RenderLayers(): only a single Layer is supported.";
    overlay_objects.clear();
  }

  // Draw the bottom layer with all of the overlay layers above it.
  DrawLayer(frame, target_presentation_time, layers[0], output_image,
            escher::Model(std::move(overlay_objects)));
}

// Helper function for DrawLayer
static escher::PaperRendererShadowType GetPaperRendererShadowType(
    fuchsia::ui::gfx::ShadowTechnique technique) {
  using escher::PaperRendererShadowType;
  using fuchsia::ui::gfx::ShadowTechnique;

  switch (technique) {
    case ShadowTechnique::UNSHADOWED:
      return PaperRendererShadowType::kNone;
    case ShadowTechnique::SCREEN_SPACE:
      return PaperRendererShadowType::kSsdo;
    case ShadowTechnique::SHADOW_MAP:
      return PaperRendererShadowType::kShadowMap;
    case ShadowTechnique::MOMENT_SHADOW_MAP:
      return PaperRendererShadowType::kMomentShadowMap;
    case ShadowTechnique::STENCIL_SHADOW_VOLUME:
      return PaperRendererShadowType::kShadowVolume;
  }
}

void EngineRenderer::DrawLayer(const escher::FramePtr& frame,
                               zx_time_t target_presentation_time, Layer* layer,
                               const escher::ImagePtr& output_image,
                               const escher::Model& overlay_model) {
  FXL_DCHECK(layer->IsDrawable());
  float stage_width = static_cast<float>(output_image->width());
  float stage_height = static_cast<float>(output_image->height());

  if (layer->size().x != stage_width || layer->size().y != stage_height) {
    // TODO(SCN-248): Should be able to render into a viewport of the
    // output image, but we're not that fancy yet.
    layer->error_reporter()->ERROR()
        << "TODO(SCN-248): scenic::gfx::EngineRenderer::DrawLayer()"
           ": layer size of "
        << layer->size().x << "x" << layer->size().y
        << " does not match output image size of " << stage_width << "x"
        << stage_height;
    return;
  }

  // TODO(SCN-1273): add pixel tests for various shadow modes (particularly
  // those implemented by PaperRenderer2).
  escher::PaperRendererShadowType shadow_type =
      GetPaperRendererShadowType(layer->renderer()->shadow_technique());
  switch (shadow_type) {
    case escher::PaperRendererShadowType::kNone:
    case escher::PaperRendererShadowType::kShadowVolume:
      break;
    default:
      FXL_LOG(WARNING) << "EngineRenderer does not support "
                       << layer->renderer()->shadow_technique()
                       << "; using UNSHADOWED.";
      shadow_type = escher::PaperRendererShadowType::kNone;
  }

  DrawLayerWithPaperRenderer2(frame, target_presentation_time, layer,
                              shadow_type, output_image, overlay_model);
}

std::vector<escher::Camera>
EngineRenderer::GenerateEscherCamerasForPaperRenderer(
    const escher::FramePtr& frame, Camera* camera,
    escher::ViewingVolume viewing_volume, zx_time_t target_presentation_time) {
  if (camera->IsKindOf<StereoCamera>()) {
    auto stereo_camera = camera->As<StereoCamera>();
    escher::Camera left_camera =
        stereo_camera->GetEscherCamera(StereoCamera::Eye::LEFT);
    escher::Camera right_camera =
        stereo_camera->GetEscherCamera(StereoCamera::Eye::RIGHT);

    escher::BufferPtr latched_pose_buffer;
    if (escher::hmd::PoseBuffer pose_buffer = camera->GetEscherPoseBuffer()) {
      latched_pose_buffer = pose_buffer_latching_shader_->LatchStereoPose(
          frame, left_camera, right_camera, pose_buffer,
          target_presentation_time);
      left_camera.SetLatchedPoseBuffer(latched_pose_buffer,
                                       escher::CameraEye::kLeft);
      right_camera.SetLatchedPoseBuffer(latched_pose_buffer,
                                        escher::CameraEye::kRight);
    }

    return {left_camera, right_camera};
  } else {
    escher::Camera escher_camera = camera->GetEscherCamera(viewing_volume);

    escher::BufferPtr latched_pose_buffer;
    if (escher::hmd::PoseBuffer pose_buffer = camera->GetEscherPoseBuffer()) {
      latched_pose_buffer = pose_buffer_latching_shader_->LatchPose(
          frame, escher_camera, pose_buffer, target_presentation_time);
      escher_camera.SetLatchedPoseBuffer(latched_pose_buffer,
                                         escher::CameraEye::kLeft);
    }

    return {escher_camera};
  }
}

void EngineRenderer::DrawLayerWithPaperRenderer2(
    const escher::FramePtr& frame, zx_time_t target_presentation_time,
    Layer* layer, const escher::PaperRendererShadowType shadow_type,
    const escher::ImagePtr& output_image, const escher::Model& overlay_model) {
  TRACE_DURATION("gfx", "EngineRenderer::DrawLayerWithPaperRenderer2");

  frame->command_buffer()->TransitionImageLayout(
      output_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);

  auto& renderer = layer->renderer();
  auto camera = renderer->camera();
  auto& scene = camera->scene();

  paper_renderer2_->SetConfig(escher::PaperRendererConfig{
      .shadow_type = shadow_type,
      .debug = renderer->enable_debugging(),
  });

  // Set up PaperScene from Scenic Scene resource.
  auto paper_scene = fxl::MakeRefCounted<escher::PaperScene>();
  paper_scene->bounding_box = layer->GetViewingVolume().bounding_box();

  // Set up ambient light.
  if (scene->ambient_lights().empty()) {
    FXL_LOG(WARNING)
        << "scenic_impl::gfx::EngineRenderer: scene has no ambient light.";
    paper_scene->ambient_light.color = escher::vec3(0, 0, 0);
  } else {
    paper_scene->ambient_light.color = scene->ambient_lights()[0]->color();
  }

  // Set up point lights.
  paper_scene->point_lights.reserve(scene->point_lights().size());
  for (auto& light : scene->point_lights()) {
    paper_scene->point_lights.push_back(escher::PaperPointLight{
        .position = light->position(),
        .color = light->color(),
        .falloff = light->falloff(),
    });
  }

  paper_renderer2_->BeginFrame(
      frame, paper_scene,
      GenerateEscherCamerasForPaperRenderer(
          frame, camera, layer->GetViewingVolume(), target_presentation_time),
      output_image);

  // TODO(SCN-1256): scene-visitation should generate cameras, collect
  // lights, etc.
  escher::BatchGpuUploader gpu_uploader(escher_, frame->frame_number());
  EngineRendererVisitor visitor(paper_renderer2_.get(), &gpu_uploader);
  visitor.Visit(camera->scene().get());

  gpu_uploader.Submit();

  // TODO(SCN-1270): support for multiple layers.
  FXL_DCHECK(overlay_model.objects().empty());

  paper_renderer2_->EndFrame();
}

escher::ImagePtr EngineRenderer::GetLayerFramebufferImage(uint32_t width,
                                                          uint32_t height) {
  escher::ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Srgb;
  info.width = width;
  info.height = height;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eSampled;
  return escher_->image_cache()->NewImage(info);
}

}  // namespace gfx
}  // namespace scenic_impl
