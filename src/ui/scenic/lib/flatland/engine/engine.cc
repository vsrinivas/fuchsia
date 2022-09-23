// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/scene_dumper.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "src/ui/scenic/lib/utils/logging.h"

// TODO(fxbug.dev/77414): for hacky invocation of OnVsync() at the end of RenderScheduledFrame().
#include <lib/zx/time.h>

#include <sstream>
#include <string>
#include <unordered_set>

// Hardcoded double buffering.
// TODO(fxbug.dev/76640): make this configurable.  Even fancier: is it worth considering sharing a
// pool of framebuffers between multiple displays?  (assuming that their dimensions are similar,
// etc.)
static constexpr uint32_t kNumDisplayFramebuffers = 2;

namespace flatland {

Engine::Engine(std::shared_ptr<DisplayCompositor> flatland_compositor,
               std::shared_ptr<FlatlandPresenterImpl> flatland_presenter,
               std::shared_ptr<UberStructSystem> uber_struct_system,
               std::shared_ptr<LinkSystem> link_system, inspect::Node inspect_node,
               GetRootTransformFunc get_root_transform)
    : flatland_compositor_(std::move(flatland_compositor)),
      flatland_presenter_(std::move(flatland_presenter)),
      uber_struct_system_(std::move(uber_struct_system)),
      link_system_(std::move(link_system)),
      inspect_node_(std::move(inspect_node)),
      get_root_transform_(std::move(get_root_transform)) {
  FX_DCHECK(flatland_compositor_);
  FX_DCHECK(flatland_presenter_);
  FX_DCHECK(uber_struct_system_);
  FX_DCHECK(link_system_);
  InitializeInspectObjects();
}

constexpr char kSceneDump[] = "scene_dump";

void Engine::InitializeInspectObjects() {
  inspect_scene_dump_ = inspect_node_.CreateLazyValues(kSceneDump, [this] {
    inspect::Inspector inspector;
    const auto root_transform = get_root_transform_();
    if (!root_transform) {
      inspector.GetRoot().CreateString(kSceneDump, "(No Root Transform)", &inspector);
      return fpromise::make_ok_promise(std::move(inspector));
    }

    const SceneState scene_state(*this, *root_transform);
    std::ostringstream output;
    DumpScene(scene_state.snapshot, scene_state.topology_data, scene_state.images,
              scene_state.image_indices, scene_state.image_rectangles, output);
    inspector.GetRoot().CreateString(kSceneDump, output.str(), &inspector);
    return fpromise::make_ok_promise(std::move(inspector));
  });
}

void Engine::RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                                  const FlatlandDisplay& display,
                                  scheduling::FrameRenderer::FramePresentedCallback callback) {
  // NOTE: This is a temporary situation; soon FlatlandDisplay will be the only way to connect
  // content to a display.
  FX_CHECK(frame_number == last_rendered_frame_ + 1);
  last_rendered_frame_ = frame_number;

  SceneState scene_state(*this, display.root_transform());
  const auto hw_display = display.display();

#if defined(USE_FLATLAND_VERBOSE_LOGGING)
  std::ostringstream str;
  str << "Engine::RenderScheduledFrame()\n"
      << "Root transform of global topology: " << scene_state.topology_data.topology_vector[0]
      << "\nTopologically-sorted transforms and their corresponding parent transforms:";
  for (size_t i = 1; i < scene_state.topology_data.topology_vector.size(); ++i) {
    str << "\n        " << scene_state.topology_data.topology_vector[i] << " -> "
        << scene_state.topology_data.topology_vector[scene_state.topology_data.parent_indices[i]];
  }
  str << "\nFrame display-list contains " << scene_state.image_rectangles.size()
      << " image-rectangles and " << scene_state.images.size() << " images.";
  for (auto& r : scene_state.image_rectangles) {
    str << "\n        rect: " << r;
  }
  for (auto& i : scene_state.images) {
    str << "\n        image: " << i;
  }
  FLATLAND_VERBOSE_LOG << str.str();
#endif

  link_system_->UpdateLinks(scene_state.topology_data.topology_vector,
                            scene_state.topology_data.live_handles, scene_state.global_matrices,
                            hw_display->device_pixel_ratio(), scene_state.snapshot);

  // TODO(fxbug.dev/76640): hack!  need a better place to call AddDisplay().
  if (hack_seen_display_ids_.find(hw_display->display_id()) == hack_seen_display_ids_.end()) {
    // This display hasn't been added to the DisplayCompositor yet.
    hack_seen_display_ids_.insert(hw_display->display_id());

    // TODO(fxbug.dev/78186): VkRenderer::ChoosePreferredPixelFormat() will choose an unusable
    // pixel format if we give it the whole list, so we hardcode ZX_PIXEL_FORMAT_ARGB_8888 for now.
    // TODO(fxbug.dev/71344): blocks 78186.  See kDefaultImageFormat in display_compositor.cc
    DisplayInfo display_info{
        .dimensions = glm::uvec2{hw_display->width_in_px(), hw_display->height_in_px()},
        //.formats = display.display()->pixel_formats()};
        .formats = {ZX_PIXEL_FORMAT_ARGB_8888}};

    fuchsia::sysmem::BufferCollectionInfo_2 render_target_info;
    flatland_compositor_->AddDisplay(hw_display, display_info,
                                     /*num_vmos*/ kNumDisplayFramebuffers, &render_target_info);
  }

  CullRectangles(&scene_state.image_rectangles, &scene_state.images, hw_display->width_in_px(),
                 hw_display->height_in_px());

  flatland_compositor_->RenderFrame(frame_number, presentation_time,
                                    {{.rectangles = std::move(scene_state.image_rectangles),
                                      .images = std::move(scene_state.images),
                                      .display_id = hw_display->display_id()}},
                                    flatland_presenter_->TakeReleaseFences(), std::move(callback));
}

view_tree::SubtreeSnapshot Engine::GenerateViewTreeSnapshot(
    const TransformHandle& root_transform) const {
  // TODO(fxbug.dev/82814): Stop generating the GlobalTopologyData twice. It's wasted work and a
  // synchronization hazard.
  const auto uber_struct_snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto link_child_to_parent_transform_map = link_system_->GetLinkChildToParentTransformMap();
  const auto link_system_id = link_system_->GetInstanceId();
  auto topology_data = GlobalTopologyData::ComputeGlobalTopologyData(
      uber_struct_snapshot, links, link_system_id, root_transform);

  const auto matrix_vector = ComputeGlobalMatrices(
      topology_data.topology_vector, topology_data.parent_indices, uber_struct_snapshot);
  const auto global_clip_regions =
      ComputeGlobalTransformClipRegions(topology_data.topology_vector, topology_data.parent_indices,
                                        matrix_vector, uber_struct_snapshot);
  topology_data.hit_regions =
      ComputeGlobalHitRegions(topology_data.topology_vector, topology_data.parent_indices,
                              matrix_vector, uber_struct_snapshot);

  return flatland::GlobalTopologyData::GenerateViewTreeSnapshot(
      topology_data, global_clip_regions, matrix_vector, link_child_to_parent_transform_map);
}

// TODO(fxbug.dev/81842) If we put Screenshot on its own thread, we should make this call thread
// safe.
Renderables Engine::GetRenderables(const FlatlandDisplay& display) {
  TransformHandle root = display.root_transform();

  SceneState scene_state(*this, root);
  const auto hw_display = display.display();
  CullRectangles(&scene_state.image_rectangles, &scene_state.images, hw_display->width_in_px(),
                 hw_display->height_in_px());

  return std::make_pair(std::move(scene_state.image_rectangles), std::move(scene_state.images));
}

Engine::SceneState::SceneState(Engine& engine, TransformHandle root_transform) {
  snapshot = engine.uber_struct_system_->Snapshot();

  const auto links = engine.link_system_->GetResolvedTopologyLinks();
  const auto link_system_id = engine.link_system_->GetInstanceId();

  topology_data = GlobalTopologyData::ComputeGlobalTopologyData(snapshot, links, link_system_id,
                                                                root_transform);
  global_matrices =
      ComputeGlobalMatrices(topology_data.topology_vector, topology_data.parent_indices, snapshot);

  auto [indices, im] =
      ComputeGlobalImageData(topology_data.topology_vector, topology_data.parent_indices, snapshot);
  this->image_indices = std::move(indices);
  this->images = std::move(im);

  const auto global_image_sample_regions = ComputeGlobalImageSampleRegions(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto global_clip_regions = ComputeGlobalTransformClipRegions(
      topology_data.topology_vector, topology_data.parent_indices, global_matrices, snapshot);

  image_rectangles =
      ComputeGlobalRectangles(SelectAttribute(global_matrices, image_indices),
                              SelectAttribute(global_image_sample_regions, image_indices),
                              SelectAttribute(global_clip_regions, image_indices), images);
}

}  // namespace flatland
