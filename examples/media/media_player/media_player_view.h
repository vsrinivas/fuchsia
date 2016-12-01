// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "apps/media/examples/media_player/media_player_params.h"
#include "apps/media/lib/mapped_shared_buffer.h"
#include "apps/media/lib/timeline_function.h"
#include "apps/media/services/media_player.fidl.h"
#include "apps/media/services/video_renderer.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
#include "lib/ftl/macros.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace examples {

class MediaPlayerView : public mozart::BaseView, public mozart::InputListener {
 public:
  MediaPlayerView(mozart::ViewManagerPtr view_manager,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  modular::ApplicationContext* application_context,
                  const MediaPlayerParams& params);

  ~MediaPlayerView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnLayout() override;
  void OnDraw() override;
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;

  // |InputListener|:
  void OnEvent(mozart::EventPtr event,
               const OnEventCallback& callback) override;

  // Draws the progress bar, etc, into the provided canvas.
  void DrawControls(SkCanvas* canvas, const SkISize& size);

  // Handles a status update from the player. When called with the default
  // argument values, initiates status updates.
  void HandleStatusUpdates(
      uint64_t version = media::MediaPlayer::kInitialStatus,
      media::MediaPlayerStatusPtr status = nullptr);

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

  mozart::InputHandler input_handler_;
  mozart::BufferProducer buffer_producer_;
  media::MediaPlayerPtr media_player_;
  media::VideoRendererPtr video_renderer_;
  mozart::ViewInfoPtr video_view_info_;
  mozart::ViewPropertiesPtr video_view_properties_;
  mozart::Size video_size_;
  uint32_t scene_version_ = 1u;
  State previous_state_ = State::kPaused;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  media::MediaMetadataPtr metadata_;
  mozart::RectF progress_bar_rect_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;

  int64_t frame_time_;
  int64_t prev_frame_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerView);
};

}  // namespace examples
