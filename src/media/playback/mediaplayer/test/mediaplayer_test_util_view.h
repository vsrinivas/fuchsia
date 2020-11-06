// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/timeline_function.h>

#include <memory>
#include <queue>

#include "src/lib/ui/base_view/base_view.h"
#include "src/media/playback/mediaplayer/test/command_queue.h"
#include "src/media/playback/mediaplayer/test/mediaplayer_test_util_params.h"

namespace media_player {
namespace test {

class MediaPlayerTestUtilView : public scenic::BaseView {
 public:
  MediaPlayerTestUtilView(scenic::ViewContext view_context, fit::function<void(int)> quit_callback,
                          const MediaPlayerTestUtilParams& params);

  ~MediaPlayerTestUtilView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(ERROR) << "Scenic Error " << error; }

  // Implements --experiment. Implementations of this method should not, in
  // general, be submitted. This is for developer experiments.
  void RunExperiment();

  // Implements --test-seek.
  void TestSeek();

  // Continues --test-seek assuming that a file is loaded and the view is ready.
  void ContinueTestSeek();

  // Schedules playback of the next file when end-of-stream is reached, if there
  // is a next file to be played.
  void ScheduleNextFile();

  // |scenic::BaseView|
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::BaseView|
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // |scenic::BaseView|
  void OnScenicEvent(fuchsia::ui::scenic::Event event) override;

  void OnChildAttached(uint32_t view_holder_id);
  void OnChildUnavailable(uint32_t view_holder_id);

  // Perform a layout of the UI elements.
  void Layout();

  // Handles a status changed event from the player.
  void HandleStatusChanged(const fuchsia::media::playback::PlayerStatus& status);

  // Toggles between play and pause.
  void TogglePlayPause();

  // Returns progress in ns.
  int64_t progress_ns() const;

  // Returns progress in the range 0.0 to 1.0.
  float normalized_progress() const;

  fit::function<void(int)> quit_callback_;
  const MediaPlayerTestUtilParams& params_;
  size_t next_path_index_ = 0;

  scenic::ShapeNode background_node_;
  scenic::ShapeNode progress_bar_node_;
  scenic::ShapeNode progress_bar_slider_node_;
  std::unique_ptr<scenic::EntityNode> video_host_node_;
  std::unique_ptr<scenic::ViewHolder> video_view_holder_;

  fuchsia::media::playback::PlayerPtr player_;
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

  // Disallow copy, assign and move.
  MediaPlayerTestUtilView(const MediaPlayerTestUtilView&) = delete;
  MediaPlayerTestUtilView(MediaPlayerTestUtilView&&) = delete;
  MediaPlayerTestUtilView& operator=(const MediaPlayerTestUtilView&) = delete;
  MediaPlayerTestUtilView& operator=(MediaPlayerTestUtilView&&) = delete;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_UTIL_VIEW_H_
