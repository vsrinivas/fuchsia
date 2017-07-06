// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "application/lib/app/application_context.h"
#include "apps/media/examples/media_player/media_player_params.h"
#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/services/media_player.fidl.h"
#include "apps/media/services/net_media_player.fidl.h"
#include "apps/media/services/video_renderer.fidl.h"
#include "apps/mozart/lib/scene/skia/host_canvas_cycler.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "lib/ftl/macros.h"

namespace examples {

class MediaPlayerView : public mozart::BaseView {
 public:
  MediaPlayerView(mozart::ViewManagerPtr view_manager,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  app::ApplicationContext* application_context,
                  const MediaPlayerParams& params);

  ~MediaPlayerView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) override;
  void OnSceneInvalidated(
      mozart2::PresentationInfoPtr presentation_info) override;
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

  // Perform a layout of the UI elements.
  void Layout();

  // Draws the progress bar, etc, into the provided canvas.
  void DrawControls(SkCanvas* canvas, const SkISize& size);

  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandlePlayerStatusUpdates(
      uint64_t version = media::MediaPlayer::kInitialStatus,
      media::MediaPlayerStatusPtr status = nullptr);

  // Handles a status update from the vidoe renderer. When called with the
  // default argument values, initiates status updates.
  void HandleVideoRendererStatusUpdates(
      uint64_t version = media::VideoRenderer::kInitialStatus,
      media::VideoRendererStatusPtr status = nullptr);

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

  mozart::client::ShapeNode background_node_;
  mozart::skia::HostCanvasCycler controls_widget_;
  std::unique_ptr<mozart::client::EntityNode> video_host_node_;

  media::NetMediaPlayerPtr net_media_player_;
  media::VideoRendererPtr video_renderer_;
  mozart::ViewPropertiesPtr video_view_properties_;
  mozart::Size video_size_;
  mozart::Size pixel_aspect_ratio_;
  State previous_state_ = State::kPaused;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  media::MediaMetadataPtr metadata_;
  mozart::Rect content_rect_;
  mozart::Rect controls_rect_;
  mozart::RectF progress_bar_rect_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;

  int64_t frame_time_;
  int64_t prev_frame_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerView);
};

}  // namespace examples
