// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"

#include <trace/event.h>

#include "lib/escher/hmd/pose_buffer_latching_shader.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/renderer/paper_renderer.h"
#include "lib/escher/renderer/semaphore.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/renderer/shadow_map_renderer.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/vk/image.h"

#include "garnet/lib/ui/gfx/engine/frame_timings.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/dump_visitor.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/stereo_camera.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo Compositor::kTypeInfo = {ResourceType::kCompositor,
                                                "Compositor"};

Compositor::Compositor(Session* session, scenic::ResourceId id,
                       const ResourceTypeInfo& type_info,
                       std::unique_ptr<Swapchain> swapchain)
    : Resource(session, id, type_info),
      escher_(session->engine()->GetEscherWeakPtr()),
      swapchain_(std::move(swapchain)),
      pose_buffer_latching_shader_(
          std::make_unique<escher::hmd::PoseBufferLatchingShader>(escher_)) {
  FXL_DCHECK(swapchain_.get());

  session->engine()->AddCompositor(this);
}

Compositor::~Compositor() { session()->engine()->RemoveCompositor(this); }

void Compositor::CollectScenes(std::set<Scene*>* scenes_out) {
  if (layer_stack_) {
    for (auto& layer : layer_stack_->layers()) {
      layer->CollectScenes(scenes_out);
    }
  }
}

bool Compositor::SetLayerStack(LayerStackPtr layer_stack) {
  layer_stack_ = std::move(layer_stack);
  return true;
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
    FXL_LOG(WARNING) << "scenic::gfx::Compositor::InitEscherStage(): no "
                        "directional light was provided (heading: "
                     << kHeading << ", elevation: " << kElevation
                     << ", intensity: " << kIntensity << ").";
    stage->set_key_light(
        escher::DirectionalLight(escher::vec2(kHeading, kElevation),
                                 0.15f * M_PI, escher::vec3(kIntensity)));
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

std::pair<uint32_t, uint32_t> Compositor::GetBottomLayerSize() const {
  std::vector<Layer*> drawable_layers = GetDrawableLayers();
  FXL_CHECK(!drawable_layers.empty()) << "No drawable layers";
  return {drawable_layers[0]->width(), drawable_layers[0]->height()};
}

int Compositor::GetNumDrawableLayers() const {
  return GetDrawableLayers().size();
}

std::vector<Layer*> Compositor::GetDrawableLayers() const {
  if (!layer_stack_) {
    return std::vector<Layer*>();
  }
  std::vector<Layer*> drawable_layers;
  for (auto& layer : layer_stack_->layers()) {
    if (layer->IsDrawable()) {
      drawable_layers.push_back(layer.get());
    }
  }

  // Sort the layers from bottom to top.
  std::sort(drawable_layers.begin(), drawable_layers.end(), [](auto a, auto b) {
    return a->translation().z < b->translation().z;
  });

  return drawable_layers;
}

std::unique_ptr<escher::Model> Compositor::DrawOverlaysToModel(
    const std::vector<Layer*>& drawable_layers, const escher::FramePtr& frame,
    const FrameTimingsPtr& frame_timings,
    escher::PaperRenderer* escher_renderer,
    escher::ShadowMapRenderer* shadow_renderer) {
  TRACE_DURATION("gfx", "Compositor::DrawOverlaysToModel");

  if (drawable_layers.empty())
    return nullptr;

  std::vector<escher::Object> layer_objects;
  layer_objects.reserve(drawable_layers.size() - 1);

  // Render each layer, except the bottom one. Create an escher::Object for
  // each layer, which will be composited as part of rendering the final
  // layer.
  auto recycler = escher()->resource_recycler();
  for (size_t i = 1; i < drawable_layers.size(); ++i) {
    auto layer = drawable_layers[i];
    auto texture = escher::Texture::New(
        recycler, GetLayerFramebufferImage(layer->width(), layer->height()),
        vk::Filter::eLinear);

    DrawLayer(frame, frame_timings, escher_renderer, shadow_renderer,
              drawable_layers[i], texture->image(), nullptr);
    auto semaphore = escher::Semaphore::New(escher()->vk_device());
    frame->SubmitPartialFrame(semaphore);
    texture->image()->SetWaitSemaphore(std::move(semaphore));

    auto material = escher::Material::New(layer->color(), std::move(texture));
    material->set_opaque(layer->opaque());

    layer_objects.push_back(escher::Object::NewRect(
        escher::Transform(layer->translation()), std::move(material)));
  }

  return std::unique_ptr<escher::Model>(
      new escher::Model(std::move(layer_objects)));
}

void Compositor::DrawLayer(const escher::FramePtr& frame,
                           const FrameTimingsPtr& frame_timings,
                           escher::PaperRenderer* escher_renderer,
                           escher::ShadowMapRenderer* shadow_map_renderer,
                           Layer* layer, const escher::ImagePtr& output_image,
                           const escher::Model* overlay_model) {
  TRACE_DURATION("gfx", "Compositor::DrawLayer");
  FXL_DCHECK(layer->IsDrawable());

  float stage_width = static_cast<float>(output_image->width());
  float stage_height = static_cast<float>(output_image->height());

  if (layer->size().x != stage_width || layer->size().y != stage_height) {
    // TODO(MZ-248): Should be able to render into a viewport of the
    // output image, but we're not that fancy yet.
    layer->error_reporter()->ERROR()
        << "TODO(MZ-248): scenic::gfx::Compositor::DrawLayer()"
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
      escher_renderer->set_shadow_type(escher::PaperRendererShadowType::kNone);
      break;
    case ::fuchsia::ui::gfx::ShadowTechnique::SCREEN_SPACE:
      escher_renderer->set_shadow_type(escher::PaperRendererShadowType::kSsdo);
      break;
    case ::fuchsia::ui::gfx::ShadowTechnique::MOMENT_SHADOW_MAP:
      FXL_DLOG(WARNING) << "Moment shadow maps not implemented";
    // Fallthrough to regular shadow maps.
    case ::fuchsia::ui::gfx::ShadowTechnique::SHADOW_MAP:
      escher_renderer->set_shadow_type(
          escher::PaperRendererShadowType::kShadowMap);

      shadow_map = shadow_map_renderer->GenerateDirectionalShadowMap(
          frame, stage, model, stage.key_light().direction(),
          stage.key_light().color());
      break;
  }

  auto draw_frame_lambda = [this, escher_renderer, frame, frame_timings, &stage,
                            &model, &output_image, &shadow_map,
                            &overlay_model](escher::Camera camera) {
    if (camera.pose_buffer()) {
      camera.SetLatchedPoseBuffer(pose_buffer_latching_shader_->LatchPose(
          frame, camera, camera.pose_buffer(),
          frame_timings->target_presentation_time()));
    }
    escher_renderer->DrawFrame(frame, stage, model, camera, output_image,
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

bool Compositor::DrawFrame(const FrameTimingsPtr& frame_timings,
                           escher::PaperRenderer* escher_renderer,
                           escher::ShadowMapRenderer* shadow_renderer) {
  TRACE_DURATION("gfx", "Compositor::DrawFrame");

  std::vector<Layer*> drawable_layers = GetDrawableLayers();
  if (drawable_layers.empty())
    return false;

  escher::FramePtr frame =
      escher()->NewFrame("Scenic Compositor", frame_timings->frame_number());

  auto overlay_model = DrawOverlaysToModel(
      drawable_layers, frame, frame_timings, escher_renderer, shadow_renderer);
  if (overlay_model == nullptr)
    return false;

  bool success = swapchain_->DrawAndPresentFrame(
      frame_timings,
      [this, frame{std::move(frame)}, frame_timings, escher_renderer,
       shadow_renderer, bottom_layer = drawable_layers[0],
       overlay = overlay_model.get()](
          const escher::ImagePtr& output_image,
          const escher::SemaphorePtr& acquire_semaphore,
          const escher::SemaphorePtr& frame_done_semaphore) {
        output_image->SetWaitSemaphore(acquire_semaphore);
        DrawLayer(frame, frame_timings, escher_renderer, shadow_renderer,
                  bottom_layer, output_image, overlay);
        frame->EndFrame(frame_done_semaphore, nullptr);
      });

  if (FXL_VLOG_IS_ON(3)) {
    std::ostringstream output;
    DumpVisitor visitor(output);
    Accept(&visitor);
    FXL_VLOG(3) << "Renderer dump\n" << output.str();
  }

  return success;
}

void Compositor::DrawToImage(escher::PaperRenderer* escher_renderer,
                             escher::ShadowMapRenderer* shadow_renderer,
                             const escher::ImagePtr& output_image,
                             const escher::SemaphorePtr& frame_done_semaphore) {
  TRACE_DURATION("gfx", "Compositor::DrawToImage");

  const std::vector<Layer*> drawable_layers = GetDrawableLayers();
  if (drawable_layers.empty()) {
    return;
  }

  auto frame_timings = fxl::MakeRefCounted<FrameTimings>();
  escher::FramePtr frame =
      escher()->NewFrame("Scenic Compositor", frame_timings->frame_number());
  auto overlay_model = DrawOverlaysToModel(
      drawable_layers, frame, frame_timings, escher_renderer, shadow_renderer);
  if (overlay_model == nullptr) {
    FXL_LOG(FATAL) << "Failed to generate overlay model";
  }
  const auto& bottom_layer = drawable_layers[0];
  DrawLayer(frame, frame_timings, escher_renderer, shadow_renderer,
            bottom_layer, output_image, overlay_model.get());
  frame->EndFrame(frame_done_semaphore, nullptr);
}

escher::ImagePtr Compositor::GetLayerFramebufferImage(uint32_t width,
                                                      uint32_t height) {
  escher::ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Srgb;
  info.width = width;
  info.height = height;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eSampled;
  return escher()->image_cache()->NewImage(info);
}

}  // namespace gfx
}  // namespace scenic
