// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media_player.h>
#include <lib/async-loop/cpp/loop.h>

#include "examples/ui/lib/host_canvas_cycler.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/ui/view_framework/base_view.h"
#include "topaz/examples/media/media_player_skia/media_player_params.h"

namespace examples {

class MediaPlayerView : public mozart::BaseView {
 public:
  MediaPlayerView(async::Loop* loop,
                  views_v1::ViewManagerPtr view_manager,
                  fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
                  component::ApplicationContext* application_context,
                  const MediaPlayerParams& params);

  ~MediaPlayerView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnPropertiesChanged(views_v1::ViewProperties old_properties) override;
  void OnSceneInvalidated(images::PresentationInfo presentation_info) override;
  void OnChildAttached(uint32_t child_key,
                       views_v1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  bool OnInputEvent(input::InputEvent event) override;

  // Perform a layout of the UI elements.
  void Layout();

  // Draws the progress bar, etc, into the provided canvas.
  void DrawControls(SkCanvas* canvas, const SkISize& size);

  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandlePlayerStatusUpdates(
      uint64_t version = media::kInitialStatus,
      media_player::MediaPlayerStatusPtr status = nullptr);

  // Toggles between play and pause.
  void TogglePlayPause();

  // Returns progress in the range 0.0 to 1.0.
  float progress() const;

  // Returns the current frame rate in frames per second.
  float frame_rate() const {
    if (frame_time_ == prev_frame_time_) {
      return 0.0f;
    }

    return float(1000000000.0 / double(frame_time_ - prev_frame_time_));
  }

  async::Loop* const loop_;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::skia::HostCanvasCycler controls_widget_;
  std::unique_ptr<scenic_lib::EntityNode> video_host_node_;

  media_player::MediaPlayerPtr media_player_;
  geometry::Size video_size_;
  geometry::Size pixel_aspect_ratio_;
  State previous_state_ = State::kPaused;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  media_player::MediaMetadataPtr metadata_;
  geometry::RectF content_rect_;
  geometry::RectF controls_rect_;
  geometry::RectF progress_bar_rect_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;

  int64_t frame_time_;
  int64_t prev_frame_time_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerView);
};

}  // namespace examples
