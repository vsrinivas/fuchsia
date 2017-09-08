// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/compositor/compositor.h"

#include <trace/event.h>

#include "escher/impl/image_cache.h"
#include "escher/renderer/image.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/renderer/semaphore_wait.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"

#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/engine/swapchain.h"
#include "garnet/bin/ui/scene_manager/resources/camera.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/layer.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/layer_stack.h"
#include "garnet/bin/ui/scene_manager/resources/dump_visitor.h"
#include "garnet/bin/ui/scene_manager/resources/renderers/renderer.h"

namespace scene_manager {

const ResourceTypeInfo Compositor::kTypeInfo = {ResourceType::kCompositor,
                                                "Compositor"};

Compositor::Compositor(Session* session,
                       scenic::ResourceId id,
                       const ResourceTypeInfo& type_info,
                       std::unique_ptr<Swapchain> swapchain)
    : Resource(session, id, type_info),
      escher_(session->engine()->escher()),
      swapchain_(std::move(swapchain)) {
  FTL_DCHECK(swapchain_.get());

  session->engine()->AddCompositor(this);
}

Compositor::~Compositor() {
  session()->engine()->RemoveCompositor(this);
}

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

void Compositor::InitStage(escher::Stage* stage,
                           uint32_t width,
                           uint32_t height) {
  // TODO(MZ-194): Define these properties on the Scene instead of hardcoding
  // them here.
  constexpr float kTop = 1000;
  constexpr float kBottom = 0;
  stage->set_viewing_volume(
      {static_cast<float>(width), static_cast<float>(height), kTop, kBottom});
  stage->set_key_light(escher::DirectionalLight(
      escher::vec2(1.5f * M_PI, 1.5f * M_PI), 0.15f * M_PI, 0.7f));
  stage->set_fill_light(escher::AmbientLight(0.3f));
}

void Compositor::DrawLayer(escher::PaperRenderer* escher_renderer,
                           Layer* layer,
                           const escher::ImagePtr& output_image,
                           const escher::SemaphorePtr& frame_done_semaphore,
                           const escher::Model* overlay_model) {
  TRACE_DURATION("gfx", "Compositor::DrawLayer");
  FTL_DCHECK(layer->IsDrawable());

  float stage_width = static_cast<float>(output_image->width());
  float stage_height = static_cast<float>(output_image->height());

  if (layer->size().x != stage_width || layer->size().y != stage_height) {
    // TODO(MZ-248): Should be able to render into a viewport of the
    // output image, but we're not that fancy yet.
    layer->error_reporter()->ERROR()
        << "TODO(MZ-248): scene_manager::Compositor::DrawLayer()"
           ": layer size of "
        << layer->size().x << "x" << layer->size().y
        << " does not match output image size of " << stage_width << "x"
        << stage_height;
    return;
  }

  escher::Stage stage;
  InitStage(&stage, output_image->width(), output_image->height());
  auto renderer = layer->renderer();
  escher::Model model(renderer->CreateDisplayList(renderer->camera()->scene(),
                                                  escher::vec2(layer->size())));
  escher::Camera camera =
      renderer->camera()->GetEscherCamera(stage.viewing_volume());

  escher_renderer->DrawFrame(stage, model, camera, output_image, overlay_model,
                             frame_done_semaphore, nullptr);
}

void Compositor::DrawFrame(const FrameTimingsPtr& frame_timings,
                           escher::PaperRenderer* escher_renderer) {
  TRACE_DURATION("gfx", "Compositor::DrawFrame");

  // Obtain a list of drawable layers.
  if (!layer_stack_)
    return;
  std::vector<Layer*> drawable_layers;
  for (auto& layer : layer_stack_->layers()) {
    if (layer->IsDrawable()) {
      drawable_layers.push_back(layer.get());
    }
  }
  if (drawable_layers.empty())
    return;

  // Sort the layers from bottom to top.
  std::sort(drawable_layers.begin(), drawable_layers.end(), [](auto a, auto b) {
    return a->translation().z < b->translation().z;
  });

  // Render each layer, except the bottom one.  Create an escher::Object for
  // each layer, which will be composited as part of rendering the final
  // layer.
  std::vector<escher::Object> layer_objects;

  layer_objects.reserve(drawable_layers.size() - 1);
  auto recycler = escher()->resource_recycler();
  for (size_t i = 1; i < drawable_layers.size(); ++i) {
    auto layer = drawable_layers[i];
    auto texture = escher::Texture::New(
        recycler, GetLayerFramebufferImage(layer->width(), layer->height()),
        vk::Filter::eLinear);

    auto semaphore = escher::Semaphore::New(escher()->vk_device());
    DrawLayer(escher_renderer, drawable_layers[i], texture->image(), semaphore,
              nullptr);
    texture->image()->SetWaitSemaphore(std::move(semaphore));

    auto material = escher::Material::New(layer->color(), std::move(texture));
    material->set_opaque(layer->opaque());

    layer_objects.push_back(escher::Object::NewRect(
        escher::Transform(layer->translation()), std::move(material)));
  }
  escher::Model overlay_model(std::move(layer_objects));

  swapchain_->DrawAndPresentFrame(
      frame_timings,
      [
        this, escher_renderer, layer = drawable_layers[0],
        overlay = &overlay_model
      ](const escher::ImagePtr& output_image,
        const escher::SemaphorePtr& acquire_semaphore,
        const escher::SemaphorePtr& frame_done_semaphore) {
        output_image->SetWaitSemaphore(acquire_semaphore);
        DrawLayer(escher_renderer, layer, output_image, frame_done_semaphore,
                  overlay);
      });

  if (FTL_VLOG_IS_ON(3)) {
    std::ostringstream output;
    DumpVisitor visitor(output);
    Accept(&visitor);
    FTL_VLOG(3) << "Renderer dump\n" << output.str();
  }
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

}  // namespace scene_manager
