// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "apps/media/cpp/mapped_shared_buffer.h"
#include "apps/media/cpp/timeline_function.h"
#include "apps/media/cpp/video_renderer.h"
#include "apps/media/examples/video_player/video_player_params.h"
#include "apps/media/interfaces/media_player.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/lib/view_framework/input_handler.h"
#include "lib/ftl/macros.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace examples {

class VideoPlayerView : public mozart::BaseView, public mozart::InputListener {
 public:
  VideoPlayerView(mozart::ViewManagerPtr view_manager,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  modular::ApplicationContext* application_context,
                  const VideoPlayerParams& params);

  ~VideoPlayerView() override;

 private:
  static constexpr float kMargin = 3.0f;
  static constexpr float kProgressBarHeight = 40.0f;
  static constexpr float kSymbolVerticalSpacing = 20.0f;
  static constexpr float kSymbolWidth = 30.0f;
  static constexpr float kSymbolHeight = 40.0f;
  static constexpr uint32_t kColorGray = 0xffaaaaaa;
  static constexpr uint32_t kColorBlue = 0xff5555ff;
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnDraw() override;

  // |InputListener|:
  void OnEvent(mozart::EventPtr event,
               const OnEventCallback& callback) override;

  // Creates a node for the skia drawing.
  mozart::NodePtr MakeSkiaNode(
      uint32_t resource_id,
      const mozart::RectF rect,
      const std::function<void(const mozart::Size&, SkCanvas*)> content_drawer,
      const mozart::SceneUpdatePtr& update);

  // Draws the progress bar, etc, into the provided canvas.
  void DrawSkiaContent(const mozart::Size& size, SkCanvas* canvas);

  // Creates a node for the video.
  mozart::NodePtr MakeVideoNode(mozart::TransformPtr transform,
                                const mozart::SceneUpdatePtr& update);

  // Draws the video texture image and returns its resource. |presentation_time|
  // is in nanoseconds.
  mozart::ResourcePtr DrawVideoTexture(const mozart::Size& size,
                                       int64_t presentation_time);

  // Ensures that buffer_ points to a buffer of the indicated size.
  void EnsureBuffer(const mozart::Size& size);

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
  media::MappedSharedBuffer buffer_;
  mozart::Size buffer_size_;
  media::VideoRenderer video_renderer_;
  media::MediaPlayerPtr media_player_;
  State previous_state_ = State::kPaused;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  media::MediaMetadataPtr metadata_;
  mozart::RectF progress_bar_rect_;
  bool metadata_shown_ = false;
  bool problem_shown_ = false;

  int64_t frame_time_;
  int64_t prev_frame_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VideoPlayerView);
};

}  // namespace examples
