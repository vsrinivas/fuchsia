// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/engine_renderer.h"

#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/stereo_camera.h"
#include "lib/escher/hmd/pose_buffer_latching_shader.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/renderer/paper_renderer.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/renderer/shadow_map_renderer.h"
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
      paper_renderer_(escher::PaperRenderer::New(escher_)),
      shadow_renderer_(
          escher::ShadowMapRenderer::New(escher_, paper_renderer_->model_data(),
                                         paper_renderer_->model_renderer())),
      pose_buffer_latching_shader_(
          std::make_unique<escher::hmd::PoseBufferLatchingShader>(escher_)) {
  paper_renderer_->set_sort_by_pipeline(false);
}

EngineRenderer::~EngineRenderer() = default;

void EngineRenderer::RenderLayers(const escher::FramePtr& frame,
                                  zx_time_t target_presentation_time,
                                  const escher::ImagePtr& output_image,
                                  const std::vector<Layer*>& layers) {
  // TODO(SCN-1119): change this to say "EngineRenderer::RenderLayers".
  TRACE_DURATION("gfx", "EngineRenderer::RenderLayers");

  auto overlay_model =
      DrawOverlaysToModel(layers, frame, target_presentation_time);
  const auto& bottom_layer = layers[0];
  DrawLayer(frame, target_presentation_time, bottom_layer, output_image,
            overlay_model.get());
}

std::unique_ptr<escher::Model> EngineRenderer::DrawOverlaysToModel(
    const std::vector<Layer*>& layers, const escher::FramePtr& frame,
    zx_time_t target_presentation_time) {
  TRACE_DURATION("gfx", "EngineRenderer::DrawOverlaysToModel");
  FXL_DCHECK(!layers.empty());
  if (layers.size() == 1) {
    return nullptr;
  }

  std::vector<escher::Object> layer_objects;
  layer_objects.reserve(layers.size() - 1);

  // Render each layer, except the bottom one. Create an escher::Object for
  // each layer, which will be composited as part of rendering the final
  // layer.
  auto recycler = escher_->resource_recycler();
  for (size_t i = 1; i < layers.size(); ++i) {
    auto layer = layers[i];
    auto texture = escher::Texture::New(
        recycler, GetLayerFramebufferImage(layer->width(), layer->height()),
        vk::Filter::eLinear);

    DrawLayer(frame, target_presentation_time, layers[i], texture->image(),
              nullptr);

    // TODO(SCN-1093): it would be preferable to insert barriers instead of
    // using semaphores.
    auto semaphore = escher::Semaphore::New(escher_->vk_device());
    frame->SubmitPartialFrame(semaphore);
    texture->image()->SetWaitSemaphore(std::move(semaphore));

    auto material = escher::Material::New(layer->color(), std::move(texture));
    material->set_opaque(layer->opaque());

    layer_objects.push_back(escher::Object::NewRect(
        escher::Transform(layer->translation()), std::move(material)));
  }

  return std::make_unique<escher::Model>(std::move(layer_objects));
}

// Helper function for DrawLayer().
static void InitEscherStage(
    escher::Stage* stage, const escher::ViewingVolume& viewing_volume,
    const std::vector<AmbientLightPtr>& ambient_lights,
    const std::vector<DirectionalLightPtr>& directional_lights) {
  stage->set_viewing_volume(viewing_volume);

  if (ambient_lights.empty()) {
    constexpr float kIntensity = 0.3f;
    FXL_LOG(WARNING) << "scenic::gfx::Compositor::InitEscherStage(): no "
                        "ambient light was provided.  Using one with "
                        "intensity: "
                     << kIntensity << ".";
    stage->set_fill_light(escher::AmbientLight(kIntensity));
  } else {
    if (ambient_lights.size() > 1) {
      FXL_LOG(WARNING)
          << "scenic::gfx::Compositor::InitEscherStage(): only a single "
             "ambient light is supported, but "
          << ambient_lights.size() << " were provided.  Using the first one.";
    }
    stage->set_fill_light(escher::AmbientLight(ambient_lights[0]->color()));
  }

  if (directional_lights.empty()) {
    constexpr float kHeading = 1.5f * M_PI;
    constexpr float kElevation = 1.5f * M_PI;
    constexpr float kIntensity = 0.3f;
    constexpr float kDispersion = 0.15f * M_PI;
    FXL_LOG(WARNING) << "scenic::gfx::Compositor::InitEscherStage(): no "
                        "directional light was provided (heading: "
                     << kHeading << ", elevation: " << kElevation
                     << ", intensity: " << kIntensity << ").";
    stage->set_key_light(
        escher::DirectionalLight(escher::vec2(kHeading, kElevation),
                                 kDispersion, escher::vec3(kIntensity)));
  } else {
    if (directional_lights.size() > 1) {
      FXL_LOG(WARNING)
          << "scenic::gfx::Compositor::InitEscherStage(): only a single "
             "directional light is supported, but "
          << directional_lights.size()
          << " were provided.  Using the first one.";
    }
    auto& light = directional_lights[0];
    stage->set_key_light(escher::DirectionalLight(
        light->direction(), 0.15f * M_PI, light->color()));
  }
}

void EngineRenderer::DrawLayer(const escher::FramePtr& frame,
                               zx_time_t target_presentation_time, Layer* layer,
                               const escher::ImagePtr& output_image,
                               const escher::Model* overlay_model) {
  TRACE_DURATION("gfx", "EngineRenderer::DrawLayer");
  FXL_DCHECK(layer->IsDrawable());

  float stage_width = static_cast<float>(output_image->width());
  float stage_height = static_cast<float>(output_image->height());

  if (layer->size().x != stage_width || layer->size().y != stage_height) {
    // TODO(SCN-248): Should be able to render into a viewport of the
    // output image, but we're not that fancy yet.
    layer->error_reporter()->ERROR()
        << "TODO(MZ-248): scenic::gfx::EngineRenderer::DrawLayer()"
           ": layer size of "
        << layer->size().x << "x" << layer->size().y
        << " does not match output image size of " << stage_width << "x"
        << stage_height;
    return;
  }

  auto& renderer = layer->renderer();
  auto& scene = renderer->camera()->scene();

  escher::Stage stage;
  InitEscherStage(&stage, layer->GetViewingVolume(), scene->ambient_lights(),
                  scene->directional_lights());
  escher::Model model(renderer->CreateDisplayList(renderer->camera()->scene(),
                                                  escher::vec2(layer->size())));

  // Set the renderer's shadow mode, and generate a shadow map if necessary.
  escher::ShadowMapPtr shadow_map;
  switch (renderer->shadow_technique()) {
    case ::fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED:
      paper_renderer_->set_shadow_type(escher::PaperRendererShadowType::kNone);
      break;
    case ::fuchsia::ui::gfx::ShadowTechnique::SCREEN_SPACE:
      paper_renderer_->set_shadow_type(escher::PaperRendererShadowType::kSsdo);
      break;
    case ::fuchsia::ui::gfx::ShadowTechnique::MOMENT_SHADOW_MAP:
      FXL_DLOG(WARNING) << "Moment shadow maps not implemented";
    // Fallthrough to regular shadow maps.
    case ::fuchsia::ui::gfx::ShadowTechnique::SHADOW_MAP:
      paper_renderer_->set_shadow_type(
          escher::PaperRendererShadowType::kShadowMap);

      shadow_map = shadow_renderer_->GenerateDirectionalShadowMap(
          frame, stage, model, stage.key_light().direction(),
          stage.key_light().color());
      break;
  }

  auto draw_frame_lambda = [this, paper_renderer{paper_renderer_.get()}, frame,
                            target_presentation_time, &stage, &model,
                            &output_image, &shadow_map,
                            &overlay_model](escher::Camera camera) {
    if (camera.pose_buffer()) {
      camera.SetLatchedPoseBuffer(pose_buffer_latching_shader_->LatchPose(
          frame, camera, camera.pose_buffer(), target_presentation_time));
    }
    paper_renderer->DrawFrame(frame, stage, model, camera, output_image,
                              shadow_map, overlay_model);
  };

  if (renderer->camera()->IsKindOf<StereoCamera>()) {
    auto stereo_camera = renderer->camera()->As<StereoCamera>();
    for (const auto eye : {StereoCamera::Eye::LEFT, StereoCamera::Eye::RIGHT}) {
      escher::Camera camera = stereo_camera->GetEscherCamera(eye);
      draw_frame_lambda(camera);
    }
  } else {
    escher::Camera camera =
        renderer->camera()->GetEscherCamera(stage.viewing_volume());
    draw_frame_lambda(camera);
  }
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
