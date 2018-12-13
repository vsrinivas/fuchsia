// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_
#define GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <memory>
#include <queue>
#include "garnet/bin/mediaplayer/test/command_queue.h"
#include "garnet/bin/mediaplayer/test/mediaplayer_test_util_params.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/ui/base_view/cpp/v1_base_view.h"

namespace media_player {
namespace test {

class MediaPlayerTestUtilView : public scenic::V1BaseView {
 public:
  MediaPlayerTestUtilView(scenic::ViewContext view_context,
                          fit::function<void(int)> quit_callback,
                          const MediaPlayerTestUtilParams& params);

  ~MediaPlayerTestUtilView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // Implements --experiment. Implementations of this method should not, in
  // general, be submitted. This is for developer experiments.
  void RunExperiment();

  // Implements --test-seek.
  void TestSeek();

  // Continues --test-seek assuming that a URL is loaded and the view is ready.
  void ContinueTestSeek();

  // Schedules playback of the next URL when end-of-stream is reached, if there
  // is a next URL to be played.
  void ScheduleNextUrl();

  // |scenic::V1BaseView|
  void OnPropertiesChanged(
      ::fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
  void OnChildAttached(
      uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // Perform a layout of the UI elements.
  void Layout();

  // Handles a status changed event from the player.
  void HandleStatusChanged(const fuchsia::mediaplayer::PlayerStatus& status);

  // Toggles between play and pause.
  void TogglePlayPause();

  // Returns progress in ns.
  int64_t progress_ns() const;

  // Returns progress in the range 0.0 to 1.0.
  float normalized_progress() const;

  fit::function<void(int)> quit_callback_;
  const MediaPlayerTestUtilParams& params_;
  size_t next_url_index_ = 0;

  scenic::ShapeNode background_node_;
  scenic::ShapeNode progress_bar_node_;
  scenic::ShapeNode progress_bar_slider_node_;
  std::unique_ptr<scenic::EntityNode> video_host_node_;

  fuchsia::mediaplayer::PlayerPtr player_;
  CommandQueue commands_;
  fuchsia::math::Size video_size_;
  fuchsia::math::Size pixel_aspect_ratio_;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  int64_t duration_ns_ = 0;
  fuchsia::media::MetadataPtr metadata_;
  fuchsia::math::RectF content_rect_;
  fuchsia::math::RectF controls_rect_;
  bool problem_shown_ = false;
  bool scenic_ready_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerTestUtilView);
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_
