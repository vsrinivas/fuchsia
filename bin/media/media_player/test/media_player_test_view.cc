// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/media_player_test_view.h"

#include <fcntl.h>
#include <hid/usages.h>

#include "garnet/bin/media/media_player/test/media_player_test_params.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/url/gurl.h"

#include "garnet/bin/media/media_player/framework/formatting.h"

namespace media_player {
namespace test {

namespace {
constexpr uint32_t kVideoChildKey = 0u;

constexpr int32_t kDefaultWidth = 640;
constexpr int32_t kDefaultHeight = 100;

constexpr float kBackgroundElevation = 0.f;
constexpr float kVideoElevation = 1.0f;
constexpr float kProgressBarElevation = 1.0f;
constexpr float kProgressBarSliderElevation = 2.0f;

constexpr float kControlsGap = 12.0f;
constexpr float kControlsHeight = 36.0f;

// Determines whether the rectangle contains the point x,y.
bool Contains(const fuchsia::math::RectF& rect, float x, float y) {
  return rect.x <= x && rect.y <= y && rect.x + rect.width >= x &&
         rect.y + rect.height >= y;
}

int64_t rand_less_than(int64_t limit) {
  return (static_cast<int64_t>(std::rand()) * RAND_MAX + std::rand()) % limit;
}

}  // namespace

MediaPlayerTestView::MediaPlayerTestView(
    fit::function<void(int)> quit_callback,
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    component::StartupContext* startup_context,
    const MediaPlayerTestParams& params)
    : mozart::BaseView(std::move(view_manager), std::move(view_owner_request),
                       "Media Player"),
      quit_callback_(std::move(quit_callback)),
      params_(params),
      background_node_(session()),
      progress_bar_node_(session()),
      progress_bar_slider_node_(session()) {
  FXL_DCHECK(quit_callback_);
  FXL_DCHECK(params_.is_valid());
  FXL_DCHECK(!params_.urls().empty());

  scenic::Material background_material(session());
  background_material.SetColor(0x00, 0x00, 0x00, 0xff);
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);

  scenic::Material progress_bar_material(session());
  progress_bar_material.SetColor(0x23, 0x23, 0x23, 0xff);
  progress_bar_node_.SetMaterial(progress_bar_material);
  parent_node().AddChild(progress_bar_node_);

  scenic::Material progress_bar_slider_material(session());
  progress_bar_slider_material.SetColor(0x00, 0x00, 0xff, 0xff);
  progress_bar_slider_node_.SetMaterial(progress_bar_slider_material);
  parent_node().AddChild(progress_bar_slider_node_);

  // We start with a non-zero size so we get a progress bar regardless of
  // whether we get video.
  video_size_.width = 0;
  video_size_.height = 0;
  pixel_aspect_ratio_.width = 1;
  pixel_aspect_ratio_.height = 1;

  // Create a player from all that stuff.
  media_player_ =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediaplayer::MediaPlayer>();

  media_player_.events().StatusChanged =
      [this](fuchsia::mediaplayer::MediaPlayerStatus status) {
        HandleStatusChanged(status);
      };

  ::fuchsia::ui::viewsv1token::ViewOwnerPtr video_view_owner;
  media_player_->CreateView(
      startup_context
          ->ConnectToEnvironmentService<::fuchsia::ui::viewsv1::ViewManager>()
          .Unbind(),
      video_view_owner.NewRequest());

  zx::eventpair video_host_import_token;
  video_host_node_.reset(new scenic::EntityNode(session()));
  video_host_node_->ExportAsRequest(&video_host_import_token);
  parent_node().AddChild(*video_host_node_);
  GetViewContainer()->AddChild(kVideoChildKey, std::move(video_view_owner),
                               std::move(video_host_import_token));

  SetUrl(params_.urls().front());

  if (params_.auto_play()) {
    media_player_->Play();
  } else {
    media_player_->Pause();
  }
}

MediaPlayerTestView::~MediaPlayerTestView() {}

bool MediaPlayerTestView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  bool handled = false;
  if (event.is_pointer()) {
    const auto& pointer = event.pointer();
    if (pointer.phase == fuchsia::ui::input::PointerEventPhase::DOWN) {
      if (metadata_ && Contains(controls_rect_, pointer.x, pointer.y)) {
        // User poked the progress bar...seek.
        media_player_->Seek((pointer.x - controls_rect_.x) *
                            metadata_->duration / controls_rect_.width);
        if (state_ != State::kPlaying) {
          media_player_->Play();
        }
      } else {
        // User poked elsewhere.
        TogglePlayPause();
      }

      handled = true;
    }
  } else if (event.is_keyboard()) {
    auto& keyboard = event.keyboard();
    if (keyboard.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED) {
      switch (keyboard.hid_usage) {
        case HID_USAGE_KEY_SPACE:
          TogglePlayPause();
          handled = true;
          break;
        case HID_USAGE_KEY_Q:
          quit_callback_(0);
          handled = true;
          break;
        default:
          break;
      }
    }
  }

  return handled;
}

void MediaPlayerTestView::OnPropertiesChanged(
    ::fuchsia::ui::viewsv1::ViewProperties old_properties) {
  Layout();
}

void MediaPlayerTestView::SetUrl(const std::string url_as_string) {
  url::GURL url = url::GURL(url_as_string);

  if (url.SchemeIsFile()) {
    media_player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(
        fxl::UniqueFD(open(url.path().c_str(), O_RDONLY)).get()));
  } else {
    media_player_->SetHttpSource(url_as_string);
  }
}

void MediaPlayerTestView::Layout() {
  if (!has_logical_size())
    return;

  // Make the background fill the space.
  scenic::Rectangle background_shape(session(), logical_size().width,
                                     logical_size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(logical_size().width * .5f,
                                  logical_size().height * .5f,
                                  kBackgroundElevation);

  // Compute maximum size of video content after reserving space
  // for decorations.
  fuchsia::math::SizeF max_content_size;
  max_content_size.width = logical_size().width;
  max_content_size.height =
      logical_size().height - kControlsHeight - kControlsGap;

  // Shrink video to fit if needed.
  uint32_t video_width =
      (video_size_.width == 0 ? kDefaultWidth : video_size_.width) *
      pixel_aspect_ratio_.width;
  uint32_t video_height =
      (video_size_.height == 0 ? kDefaultHeight : video_size_.height) *
      pixel_aspect_ratio_.height;

  if (max_content_size.width * video_height <
      max_content_size.height * video_width) {
    content_rect_.width = max_content_size.width;
    content_rect_.height = video_height * max_content_size.width / video_width;
  } else {
    content_rect_.width = video_width * max_content_size.height / video_height;
    content_rect_.height = max_content_size.height;
  }

  // Position the video.
  content_rect_.x = (logical_size().width - content_rect_.width) / 2.0f;
  content_rect_.y = (logical_size().height - content_rect_.height -
                     kControlsHeight - kControlsGap) /
                    2.0f;

  // Position the controls.
  controls_rect_.x = content_rect_.x;
  controls_rect_.y = content_rect_.y + content_rect_.height + kControlsGap;
  controls_rect_.width = content_rect_.width;
  controls_rect_.height = kControlsHeight;

  // Put the progress bar under the content.
  scenic::Rectangle progress_bar_shape(session(), controls_rect_.width,
                                       controls_rect_.height);
  progress_bar_node_.SetShape(progress_bar_shape);
  progress_bar_node_.SetTranslation(
      controls_rect_.x + controls_rect_.width * 0.5f,
      controls_rect_.y + controls_rect_.height * 0.5f, kProgressBarElevation);

  // Put the progress bar slider on top of the progress bar.
  scenic::Rectangle progress_bar_slider_shape(session(), controls_rect_.width,
                                              controls_rect_.height);
  progress_bar_slider_node_.SetShape(progress_bar_slider_shape);
  progress_bar_slider_node_.SetTranslation(
      controls_rect_.x + controls_rect_.width * 0.5f,
      controls_rect_.y + controls_rect_.height * 0.5f,
      kProgressBarSliderElevation);

  // Ask the view to fill the space.
  ::fuchsia::ui::viewsv1::ViewProperties view_properties;
  view_properties.view_layout = ::fuchsia::ui::viewsv1::ViewLayout::New();
  view_properties.view_layout->size.width = content_rect_.width;
  view_properties.view_layout->size.height = content_rect_.height;
  GetViewContainer()->SetChildProperties(
      kVideoChildKey, fidl::MakeOptional(std::move(view_properties)));

  InvalidateScene();
}

void MediaPlayerTestView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_physical_size())
    return;

  // Position the video.
  if (video_host_node_) {
    video_host_node_->SetTranslation(content_rect_.x, content_rect_.y,
                                     kVideoElevation);
  }

  float progress_bar_slider_width =
      controls_rect_.width * normalized_progress();
  scenic::Rectangle progress_bar_slider_shape(
      session(), progress_bar_slider_width, controls_rect_.height);
  progress_bar_slider_node_.SetShape(progress_bar_slider_shape);
  progress_bar_slider_node_.SetTranslation(
      controls_rect_.x + progress_bar_slider_width * 0.5f,
      controls_rect_.y + controls_rect_.height * 0.5f,
      kProgressBarSliderElevation);

  if (state_ == State::kPlaying) {
    InvalidateScene();
  }

  if (in_current_seek_interval_ && progress_ns() >= seek_interval_end_) {
    // We've hit the end of the seek interval. Start a new one.
    StartNewSeekInterval();
  }
}

void MediaPlayerTestView::OnChildAttached(
    uint32_t child_key, ::fuchsia::ui::viewsv1::ViewInfo child_view_info) {
  FXL_DCHECK(child_key == kVideoChildKey);

  parent_node().AddChild(*video_host_node_);
  Layout();
}

void MediaPlayerTestView::OnChildUnavailable(uint32_t child_key) {
  FXL_DCHECK(child_key == kVideoChildKey);
  FXL_LOG(ERROR) << "Video view died unexpectedly";

  video_host_node_->Detach();
  video_host_node_.reset();

  GetViewContainer()->RemoveChild(child_key, nullptr);
  Layout();
}

void MediaPlayerTestView::HandleStatusChanged(
    const fuchsia::mediaplayer::MediaPlayerStatus& status) {
  // Process status received from the player.
  if (status.timeline_transform) {
    timeline_function_ = media::TimelineFunction(*status.timeline_transform);

    if (seek_interval_start_ != fuchsia::media::kUnspecifiedTime &&
        !in_current_seek_interval_ &&
        timeline_function_.subject_time() == seek_interval_start_) {
      // The seek issued in |StartNewSeekInterval| is now reflected in the
      // new |timeline_function_|. We can now use |timeline_function_| to
      // determine if we've hit the end of the interval.
      // TODO(dalesat): There should be a better way to determine when a seek
      // has completed.
      in_current_seek_interval_ = true;
    }
  }

  if (!status.end_of_stream) {
    state_ = (timeline_function_.subject_delta() == 0) ? State::kPaused
                                                       : State::kPlaying;
    was_at_end_of_stream_ = false;
  } else if (!was_at_end_of_stream_) {
    was_at_end_of_stream_ = true;
    OnEndOfStream();
  }

  if (status.problem) {
    if (!problem_shown_) {
      FXL_LOG(ERROR) << "PROBLEM: " << status.problem->type << ", "
                     << status.problem->details;
      problem_shown_ = true;
    }
  } else {
    problem_shown_ = false;
  }

  if (status.video_size && status.pixel_aspect_ratio &&
      (video_size_ != *status.video_size ||
       pixel_aspect_ratio_ != *status.pixel_aspect_ratio)) {
    video_size_ = *status.video_size;
    pixel_aspect_ratio_ = *status.pixel_aspect_ratio;
    Layout();
  }

  metadata_ = fidl::Clone(status.metadata);

  InvalidateScene();
}

void MediaPlayerTestView::OnEndOfStream() {
  if (params_.test_seek()) {
    StartNewSeekInterval();
    return;
  }

  if (++current_url_index_ == params_.urls().size() && !params_.loop()) {
    // No more files, not looping.
    state_ = State::kEnded;
    return;
  }

  if (current_url_index_ == params_.urls().size()) {
    current_url_index_ = 0;
  }

  if (params_.urls().size() > 1) {
    SetUrl(params_.urls()[current_url_index_]);
  } else {
    media_player_->Seek(0);
  }

  media_player_->Play();
}

void MediaPlayerTestView::TogglePlayPause() {
  switch (state_) {
    case State::kPaused:
      media_player_->Play();
      break;
    case State::kPlaying:
      media_player_->Pause();
      break;
    case State::kEnded:
      media_player_->Seek(0);
      media_player_->Play();
      break;
    default:
      break;
  }
}

int64_t MediaPlayerTestView::progress_ns() const {
  if (!metadata_ || metadata_->duration == 0) {
    return 0;
  }

  // Apply the timeline function to the current time.
  int64_t position = timeline_function_(media::Timeline::local_now());

  if (position < 0) {
    position = 0;
  }

  if (metadata_ && static_cast<uint64_t>(position) > metadata_->duration) {
    position = metadata_->duration;
  }

  return position;
}

float MediaPlayerTestView::normalized_progress() const {
  if (!metadata_ || metadata_->duration == 0) {
    return 0.0f;
  }

  return progress_ns() / static_cast<float>(metadata_->duration);
}

void MediaPlayerTestView::StartNewSeekInterval() {
  in_current_seek_interval_ = false;

  if (!metadata_ || metadata_->duration == 0) {
    // We have no duration yet. Just start over at the start of the file.
    media_player_->Seek(0);
    media_player_->Play();
    seek_interval_end_ = fuchsia::media::kUnspecifiedTime;
  }

  int64_t duration = metadata_->duration;

  // For the start position, generate a number in the range [0..duration] with
  // a 10% chance of being zero.
  seek_interval_start_ = rand_less_than(duration + duration / 10);
  if (seek_interval_start_ >= duration) {
    seek_interval_start_ = 0;
  }

  // For the end position, choose a position between start and 10% past the
  // duration. If this value is greater than the duration, the interval
  // effectively ends at the end of the file.
  seek_interval_end_ =
      seek_interval_start_ +
      rand_less_than((duration + duration / 10) - seek_interval_start_);

  if (seek_interval_end_ >= duration) {
    FXL_LOG(INFO) << "Seek interval " << AsNs(seek_interval_start_)
                  << " to end";
  } else {
    FXL_LOG(INFO) << "Seek interval " << AsNs(seek_interval_start_) << " to "
                  << AsNs(seek_interval_end_);
  }

  media_player_->Seek(seek_interval_start_);
  media_player_->Play();
}

}  // namespace test
}  // namespace media_player
