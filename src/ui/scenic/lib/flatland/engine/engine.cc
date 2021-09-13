// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

// TODO(fxbug.dev/77414): for hacky invocation of OnVsync() at the end of RenderScheduledFrame().
#include <lib/zx/time.h>

// Hardcoded double buffering.
// TODO(fxbug.dev/76640): make this configurable.  Even fancier: is it worth considering sharing a
// pool of framebuffers between multiple displays?  (assuming that their dimensions are similar,
// etc.)
static constexpr uint32_t kNumDisplayFramebuffers = 2;

namespace flatland {

Engine::Engine(std::shared_ptr<flatland::DisplayCompositor> flatland_compositor,
               std::shared_ptr<flatland::DefaultFlatlandPresenter> flatland_presenter,
               std::shared_ptr<flatland::UberStructSystem> uber_struct_system,
               std::shared_ptr<flatland::LinkSystem> link_system)
    : flatland_compositor_(std::move(flatland_compositor)),
      flatland_presenter_(std::move(flatland_presenter)),
      uber_struct_system_(std::move(uber_struct_system)),
      link_system_(std::move(link_system)) {
  FX_DCHECK(flatland_compositor_);
  FX_DCHECK(flatland_presenter_);
  FX_DCHECK(uber_struct_system_);
  FX_DCHECK(link_system_);
}

void Engine::RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                                  const FlatlandDisplay& display,
                                  scheduling::FrameRenderer::FramePresentedCallback callback) {
  // NOTE: this will fail if there exists a Gfx DisplayCompositor which renders some frames, which
  // is later replaced by a FlatlandDisplay, as this will result in a gap in frame numbers.  This is
  // a temporary situation; soon FlatlandDisplay will be the only way to connect content to a
  // display.
  FX_CHECK(frame_number == last_rendered_frame_ + 1);
  last_rendered_frame_ = frame_number;

  const auto snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto link_system_id = link_system_->GetInstanceId();

  const auto topology_data = flatland::GlobalTopologyData::ComputeGlobalTopologyData(
      snapshot, links, link_system_id, display.root_transform());
  const auto global_matrices = flatland::ComputeGlobalMatrices(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto [image_indices, images] = flatland::ComputeGlobalImageData(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto global_image_sample_regions = ComputeGlobalImageSampleRegions(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto global_clip_regions = ComputeGlobalTransformClipRegions(
      topology_data.topology_vector, topology_data.parent_indices, snapshot);

  const auto image_rectangles = flatland::ComputeGlobalRectangles(
      flatland::SelectAttribute(global_matrices, image_indices),
      flatland::SelectAttribute(global_image_sample_regions, image_indices),
      flatland::SelectAttribute(global_clip_regions, image_indices), images);

  const auto hw_display = display.display();

  // TODO(fxbug.dev/78201): we hardcode the pixel scale to {1, 1}.  We might want to augment the
  // FIDL API to allow this to be modified.
  link_system_->UpdateLinks(topology_data.topology_vector, topology_data.live_handles,
                            global_matrices, /*display_pixel_scale*/ glm::vec2{1.f, 1.f}, snapshot);

  // TODO(fxbug.dev/76640): hack!  need a better place to call AddDisplay().
  if (hack_seen_display_ids_.find(hw_display->display_id()) == hack_seen_display_ids_.end()) {
    // This display hasn't been added to the DisplayCompositor yet.
    hack_seen_display_ids_.insert(hw_display->display_id());

    // TODO(fxbug.dev/78186): VkRenderer::ChoosePreferredPixelFormat() will choose an unusable
    // pixel format if we give it the whole list, so we hardcode ZX_PIXEL_FORMAT_ARGB_8888 for now.
    // TODO(fxbug.dev/71344): blocks 78186.  See kdefaultImageFormat in display_compositor.cc
    DisplayInfo display_info{
        .dimensions = glm::uvec2{hw_display->width_in_px(), hw_display->height_in_px()},
        //.formats = display.display()->pixel_formats()};
        .formats = {ZX_PIXEL_FORMAT_ARGB_8888}};

    fuchsia::sysmem::BufferCollectionInfo_2 render_target_info;
    flatland_compositor_->AddDisplay(hw_display->display_id(), display_info,
                                     /*num_vmos*/ kNumDisplayFramebuffers, &render_target_info);
  }

  flatland_compositor_->RenderFrame(frame_number, presentation_time,
                                    {{.rectangles = std::move(image_rectangles),
                                      .images = std::move(images),
                                      .display_id = hw_display->display_id()}},
                                    flatland_presenter_->TakeReleaseFences(), std::move(callback));
}

view_tree::SubtreeSnapshot Engine::GenerateViewTreeSnapshot(const FlatlandDisplay& display) const {
  // TODO(fxbug.dev/82814): Stop generating the GlobalTopologyData twice. It's wasted work and a
  // synchronization hazard.
  const auto uber_struct_snapshot = uber_struct_system_->Snapshot();
  const auto links = link_system_->GetResolvedTopologyLinks();
  const auto link_system_id = link_system_->GetInstanceId();
  const auto topology_data = flatland::GlobalTopologyData::ComputeGlobalTopologyData(
      uber_struct_snapshot, links, link_system_id, display.root_transform());

  // TODO(fxbug.dev/82677): Get real bounding boxes inside of
  // topology_data.GenerateViewTreeSnapshot() function instead of grabbing the full display size
  // here.
  const auto hw_display = display.display();
  const auto display_width = static_cast<float>(hw_display->width_in_px());
  const auto display_height = static_cast<float>(hw_display->height_in_px());

  return topology_data.GenerateViewTreeSnapshot(display_width, display_height);
}

}  // namespace flatland
