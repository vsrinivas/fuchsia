// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/mediaplayer_test_util_view.h"

#include <fcntl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/media/cpp/type_converters.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>

#include <memory>

#include <hid/usages.h>

#include "src/lib/fsl/io/fd.h"
#include "src/lib/url/gurl.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/test/mediaplayer_test_util_params.h"

namespace media_player {
namespace test {

namespace {

constexpr int32_t kDefaultWidth = 640;
constexpr int32_t kDefaultHeight = 100;

constexpr float kBackgroundElevation = 0.0f;
constexpr float kVideoElevation = -1.0f;
constexpr float kProgressBarElevation = -1.0f;
constexpr float kProgressBarSliderElevation = -2.0f;

constexpr float kControlsGap = 12.0f;
constexpr float kControlsHeight = 36.0f;

// Determines whether the rectangle contains the point x,y.
bool Contains(const fuchsia::math::RectF& rect, float x, float y) {
  return rect.x <= x && rect.y <= y && rect.x + rect.width >= x && rect.y + rect.height >= y;
}

int64_t rand_less_than(int64_t limit) {
  return (static_cast<int64_t>(std::rand()) * RAND_MAX + std::rand()) % limit;
}

}  // namespace

MediaPlayerTestUtilView::MediaPlayerTestUtilView(scenic::ViewContext view_context,
                                                 fit::function<void(int)> quit_callback,
                                                 const MediaPlayerTestUtilParams& params)
    : scenic::BaseView(std::move(view_context), "Media Player"),
      quit_callback_(std::move(quit_callback)),
      params_(params),
      background_node_(session()),
      progress_bar_node_(session()),
      progress_bar_slider_node_(session()) {
  FX_DCHECK(quit_callback_);
  FX_DCHECK(params_.is_valid());
  FX_DCHECK(!params_.urls().empty());

  scenic::Material background_material(session());
  background_material.SetColor(0x00, 0x00, 0x00, 0xff);
  background_node_.SetMaterial(background_material);
  root_node().AddChild(background_node_);

  scenic::Material progress_bar_material(session());
  progress_bar_material.SetColor(0x23, 0x23, 0x23, 0xff);
  progress_bar_node_.SetMaterial(progress_bar_material);
  root_node().AddChild(progress_bar_node_);

  scenic::Material progress_bar_slider_material(session());
  progress_bar_slider_material.SetColor(0x00, 0x00, 0xff, 0xff);
  progress_bar_slider_node_.SetMaterial(progress_bar_slider_material);
  root_node().AddChild(progress_bar_slider_node_);

  // We start with a non-zero size so we get a progress bar regardless of
  // whether we get video.
  video_size_.width = 0;
  video_size_.height = 0;
  pixel_aspect_ratio_.width = 1;
  pixel_aspect_ratio_.height = 1;

  // Create a player from all that stuff.
  player_ = component_context()->svc()->Connect<fuchsia::media::playback::Player>();

  // Create the video view.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  player_->CreateView(std::move(view_token));

  video_host_node_ = std::make_unique<scenic::EntityNode>(session());
  video_view_holder_ =
      std::make_unique<scenic::ViewHolder>(session(), std::move(view_holder_token), "video view");
  video_host_node_->Attach(*video_view_holder_);

  root_node().AddChild(*video_host_node_);

  commands_.Init(player_.get());

  player_.events().OnStatusChanged = [this](fuchsia::media::playback::PlayerStatus status) {
    HandleStatusChanged(status);
  };

  if (params_.rate() != 1.0f) {
    player_->SetPlaybackRate(params_.rate());
  }

  // Seed the random number generator.
  std::srand(std::time(nullptr));

  if (params_.experiment()) {
    RunExperiment();
  } else if (params_.test_seek()) {
    TestSeek();
  } else {
    // Get the player primed now.
    commands_.SetUrl(params_.urls().front());
    commands_.Pause();
    commands_.WaitForViewReady();

    if (params_.auto_play()) {
      commands_.Play();
    }

    ScheduleNextUrl();
  }

  commands_.Execute();
}

void MediaPlayerTestUtilView::RunExperiment() {
  // Add experimental code here.
  // In general, no implementation for this method should be submitted.
}

void MediaPlayerTestUtilView::TestSeek() {
  commands_.SetUrl(params_.urls().front());
  commands_.WaitForViewReady();

  // Need to load content before deciding where to seek.
  commands_.WaitForContentLoaded();

  commands_.Invoke([this]() { ContinueTestSeek(); });
}

void MediaPlayerTestUtilView::ContinueTestSeek() {
  if (duration_ns_ == 0) {
    // We have no duration yet. Just start over at zero.
    commands_.Seek(0);
    commands_.Play();
    commands_.WaitForEndOfStream();
    commands_.Invoke([this]() { ContinueTestSeek(); });
    FX_LOGS(INFO) << "Seek interval: beginning to end";
    return;
  }

  // For the start position, generate a number in the range [0..duration_ns_]
  // with a 10% chance of being zero.player_impl.cc
  int64_t seek_interval_start = rand_less_than(duration_ns_ + duration_ns_ / 10);
  if (seek_interval_start >= duration_ns_) {
    seek_interval_start = 0;
  }

  // For the end position, choose a position between start and 10% past the
  // duration. If this value is greater than the duration, the interval
  // effectively ends at the end of the file.
  int64_t seek_interval_end =
      seek_interval_start +
      rand_less_than((duration_ns_ + duration_ns_ / 10) - seek_interval_start);

  commands_.Seek(seek_interval_start);
  commands_.Play();
  if (seek_interval_end >= duration_ns_) {
    FX_LOGS(INFO) << "Seek interval: " << AsNs(seek_interval_start) << " to end";
    commands_.WaitForEndOfStream();
  } else {
    FX_LOGS(INFO) << "Seek interval: " << AsNs(seek_interval_start) << " to "
                  << AsNs(seek_interval_end);
    commands_.WaitForSeekCompletion();
    commands_.WaitForPosition(seek_interval_end);
  }

  commands_.Invoke([this]() { ContinueTestSeek(); });
}

void MediaPlayerTestUtilView::ScheduleNextUrl() {
  if (++next_url_index_ == params_.urls().size()) {
    if (!params_.loop()) {
      // No more files, not looping.
      return;
    }

    next_url_index_ = 0;
  }

  commands_.WaitForEndOfStream();

  if (params_.urls().size() > 1) {
    commands_.SetUrl(params_.urls()[next_url_index_]);
  } else {
    // Just one file...seek to the beginning.
    commands_.Seek(0);
  }

  commands_.Play();

  commands_.Invoke([this]() { ScheduleNextUrl(); });
}

MediaPlayerTestUtilView::~MediaPlayerTestUtilView() {}

void MediaPlayerTestUtilView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  if (event.is_pointer()) {
    const auto& pointer = event.pointer();
    if (pointer.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
      if (duration_ns_ != 0 && Contains(controls_rect_, pointer.x, pointer.y)) {
        // User poked the progress bar...seek.
        player_->Seek((pointer.x - controls_rect_.x) * duration_ns_ / controls_rect_.width);
        if (state_ != State::kPlaying) {
          player_->Play();
        }
      } else {
        // User poked elsewhere.
        TogglePlayPause();
      }
    }
  } else if (event.is_keyboard()) {
    auto& keyboard = event.keyboard();
    if (keyboard.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED) {
      switch (keyboard.hid_usage) {
        case HID_USAGE_KEY_SPACE:
          TogglePlayPause();
          break;
        case HID_USAGE_KEY_Q:
          quit_callback_(0);
          break;
        default:
          break;
      }
    }
  }
}

void MediaPlayerTestUtilView::OnScenicEvent(fuchsia::ui::scenic::Event event) {
  switch (event.Which()) {
    case fuchsia::ui::scenic::Event::Tag::kGfx:
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::Tag::kViewConnected: {
          auto& evt = event.gfx().view_connected();
          OnChildAttached(evt.view_holder_id);
          break;
        }
        case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
          auto& evt = event.gfx().view_disconnected();
          OnChildUnavailable(evt.view_holder_id);
          break;
        }
        default:
          break;
      }
    default:
      break;
  }
}

void MediaPlayerTestUtilView::OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) {
  Layout();
}

void MediaPlayerTestUtilView::Layout() {
  if (!has_logical_size() || !video_view_holder_) {
    return;
  }

  if (!scenic_ready_) {
    scenic_ready_ = true;
    commands_.NotifyViewReady();
  }

  // Make the background fill the space.
  scenic::Rectangle background_shape(session(), logical_size().x, logical_size().y);
  background_node_.SetShape(background_shape);

  background_node_.SetTranslation(logical_size().x * .5f, logical_size().y * .5f,
                                  kBackgroundElevation);

  // Compute maximum size of video content after reserving space
  // for decorations.
  fuchsia::math::SizeF max_content_size;
  max_content_size.width = logical_size().x;
  max_content_size.height = logical_size().y - kControlsHeight - kControlsGap;

  // Shrink video to fit if needed.
  uint32_t video_width =
      (video_size_.width == 0 ? kDefaultWidth : video_size_.width) * pixel_aspect_ratio_.width;
  uint32_t video_height =
      (video_size_.height == 0 ? kDefaultHeight : video_size_.height) * pixel_aspect_ratio_.height;

  if (max_content_size.width * video_height < max_content_size.height * video_width) {
    content_rect_.width = max_content_size.width;
    content_rect_.height = video_height * max_content_size.width / video_width;
  } else {
    content_rect_.width = video_width * max_content_size.height / video_height;
    content_rect_.height = max_content_size.height;
  }

  // Position the video.
  content_rect_.x = (logical_size().x - content_rect_.width) / 2.0f;
  content_rect_.y =
      (logical_size().y - content_rect_.height - kControlsHeight - kControlsGap) / 2.0f;

  // Position the controls.
  controls_rect_.x = content_rect_.x;
  controls_rect_.y = content_rect_.y + content_rect_.height + kControlsGap;
  controls_rect_.width = content_rect_.width;
  controls_rect_.height = kControlsHeight;

  // Put the progress bar under the content.
  scenic::Rectangle progress_bar_shape(session(), controls_rect_.width, controls_rect_.height);
  progress_bar_node_.SetShape(progress_bar_shape);
  progress_bar_node_.SetTranslation(controls_rect_.x + controls_rect_.width * 0.5f,
                                    controls_rect_.y + controls_rect_.height * 0.5f,
                                    kProgressBarElevation);

  // Put the progress bar slider on top of the progress bar.
  scenic::Rectangle progress_bar_slider_shape(session(), controls_rect_.width,
                                              controls_rect_.height);
  progress_bar_slider_node_.SetShape(progress_bar_slider_shape);
  progress_bar_slider_node_.SetTranslation(controls_rect_.x + controls_rect_.width * 0.5f,
                                           controls_rect_.y + controls_rect_.height * 0.5f,
                                           kProgressBarSliderElevation);

  // Ask the view to fill the space.
  video_view_holder_->SetViewProperties(0, 0, 0, content_rect_.width, content_rect_.height, 1000.f,
                                        0, 0, 0, 0, 0, 0);

  InvalidateScene();
}

void MediaPlayerTestUtilView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_physical_size())
    return;

  // Position the video.
  if (video_host_node_) {
    // TODO(dalesat): Fix this when fxbug.dev/24252 is fixed. Should be:
    // video_host_node_->SetTranslation(
    //     content_rect_.x + content_rect_.width * 0.5f,
    //     content_rect_.y + content_rect_.height * 0.5f, kVideoElevation);
    video_host_node_->SetTranslation(content_rect_.x, content_rect_.y, kVideoElevation);
  }

  float progress_bar_slider_width = controls_rect_.width * normalized_progress();
  scenic::Rectangle progress_bar_slider_shape(session(), progress_bar_slider_width,
                                              controls_rect_.height);
  progress_bar_slider_node_.SetShape(progress_bar_slider_shape);
  progress_bar_slider_node_.SetTranslation(controls_rect_.x + progress_bar_slider_width * 0.5f,
                                           controls_rect_.y + controls_rect_.height * 0.5f,
                                           kProgressBarSliderElevation);

  if (state_ == State::kPlaying) {
    InvalidateScene();
  }
}

void MediaPlayerTestUtilView::OnChildAttached(uint32_t view_holder_id) {
  FX_DCHECK(view_holder_id == video_view_holder_->id());
  Layout();
}

void MediaPlayerTestUtilView::OnChildUnavailable(uint32_t view_holder_id) {
  FX_DCHECK(view_holder_id == video_view_holder_->id());
  FX_LOGS(ERROR) << "Video view died unexpectedly, quitting.";

  video_host_node_->Detach();
  video_host_node_ = nullptr;
  video_view_holder_ = nullptr;

  quit_callback_(0);
}

void MediaPlayerTestUtilView::HandleStatusChanged(
    const fuchsia::media::playback::PlayerStatus& status) {
  // Process status received from the player.
  if (status.timeline_function) {
    timeline_function_ = fidl::To<media::TimelineFunction>(*status.timeline_function);
    state_ = status.end_of_stream
                 ? State::kEnded
                 : (timeline_function_.subject_delta() == 0) ? State::kPaused : State::kPlaying;
  } else {
    state_ = State::kPaused;
  }

  commands_.NotifyStatusChanged(status);

  if (status.problem) {
    if (!problem_shown_) {
      FX_LOGS(ERROR) << "PROBLEM: " << status.problem->type << ", " << status.problem->details;
      problem_shown_ = true;
    }
  } else {
    problem_shown_ = false;
  }

  if (status.video_size && status.pixel_aspect_ratio &&
      (!fidl::Equals(video_size_, *status.video_size) ||
       !fidl::Equals(pixel_aspect_ratio_, *status.pixel_aspect_ratio))) {
    video_size_ = *status.video_size;
    pixel_aspect_ratio_ = *status.pixel_aspect_ratio;
    Layout();
  }

  duration_ns_ = status.duration;
  metadata_ = fidl::Clone(status.metadata);

  InvalidateScene();
}

void MediaPlayerTestUtilView::TogglePlayPause() {
  switch (state_) {
    case State::kPaused:
      player_->Play();
      break;
    case State::kPlaying:
      player_->Pause();
      break;
    case State::kEnded:
      player_->Seek(0);
      player_->Play();
      break;
    default:
      break;
  }
}

int64_t MediaPlayerTestUtilView::progress_ns() const {
  if (duration_ns_ == 0) {
    return 0;
  }

  // Apply the timeline function to the current time.
  int64_t position = timeline_function_(zx::clock::get_monotonic().get());

  if (position < 0) {
    position = 0;
  }

  if (position > duration_ns_) {
    position = duration_ns_;
  }

  return position;
}

float MediaPlayerTestUtilView::normalized_progress() const {
  if (duration_ns_ == 0) {
    return 0.0f;
  }

  return progress_ns() / static_cast<float>(duration_ns_);
}

}  // namespace test
}  // namespace media_player
