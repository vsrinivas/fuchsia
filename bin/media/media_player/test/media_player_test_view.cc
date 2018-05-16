// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "topaz/examples/media/media_player_skia/media_player_view.h"

#include <fcntl.h>
#include <hid/usages.h>

#include <iomanip>

#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"
#include "lib/url/gurl.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "topaz/examples/media/media_player_skia/media_player_params.h"

using media_player::MediaPlayer;
using media_player::NetMediaService;

namespace examples {

namespace {
constexpr uint32_t kVideoChildKey = 0u;

constexpr int32_t kDefaultWidth = 640;
constexpr int32_t kDefaultHeight = 100;

constexpr float kBackgroundElevation = 0.f;
constexpr float kVideoElevation = 1.f;
constexpr float kControlsElevation = 1.f;

constexpr float kMargin = 4.0f;
constexpr float kControlsHeight = 36.0f;
constexpr float kSymbolWidth = 24.0f;
constexpr float kSymbolHeight = 24.0f;
constexpr float kSymbolPadding = 12.0f;

constexpr SkColor kProgressBarForegroundColor = 0xff673ab7;  // Deep Purple 500
constexpr SkColor kProgressBarBackgroundColor = 0xffb39ddb;  // Deep Purple 200
constexpr SkColor kProgressBarSymbolColor = 0xffffffff;

// Determines whether the rectangle contains the point x,y.
bool Contains(const geometry::RectF& rect, float x, float y) {
  return rect.x <= x && rect.y <= y && rect.x + rect.width >= x &&
         rect.y + rect.height >= y;
}

}  // namespace

MediaPlayerView::MediaPlayerView(
    async::Loop* loop,
    views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
    component::ApplicationContext* application_context,
    const MediaPlayerParams& params)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Media Player"),

      loop_(loop),
      background_node_(session()),
      controls_widget_(session()) {
  FXL_DCHECK(loop);
  FXL_DCHECK(params.is_valid());

  scenic_lib::Material background_material(session());
  background_material.SetColor(0x1a, 0x23, 0x7e, 0xff);  // Indigo 900
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);

  parent_node().AddChild(controls_widget_);

  // We start with a non-zero size so we get a progress bar regardless of
  // whether we get video.
  video_size_.width = 0;
  video_size_.height = 0;
  pixel_aspect_ratio_.width = 1;
  pixel_aspect_ratio_.height = 1;

  if (params.device_name().empty()) {
    // Create a player from all that stuff.
    media_player_ =
        application_context->ConnectToEnvironmentService<MediaPlayer>();

    views_v1_token::ViewOwnerPtr video_view_owner;
    media_player_->CreateView(
        application_context
            ->ConnectToEnvironmentService<views_v1::ViewManager>()
            .Unbind(),
        video_view_owner.NewRequest());

    zx::eventpair video_host_import_token;
    video_host_node_.reset(new scenic_lib::EntityNode(session()));
    video_host_node_->ExportAsRequest(&video_host_import_token);
    parent_node().AddChild(*video_host_node_);
    GetViewContainer()->AddChild(kVideoChildKey, std::move(video_view_owner),
                                 std::move(video_host_import_token));

    if (!params.service_name().empty()) {
      auto net_media_service =
          application_context->ConnectToEnvironmentService<NetMediaService>();

      fidl::InterfaceHandle<MediaPlayer> media_player_handle;
      media_player_->AddBinding(media_player_handle.NewRequest());

      net_media_service->PublishMediaPlayer(params.service_name(),
                                            std::move(media_player_handle));
    }
  } else {
    // Create a player proxy.
    auto net_media_service =
        application_context->ConnectToEnvironmentService<NetMediaService>();

    net_media_service->CreateMediaPlayerProxy(params.device_name(),
                                              params.service_name(),
                                              media_player_.NewRequest());
  }

  if (!params.url().empty()) {
    url::GURL url = url::GURL(params.url());

    if (url.SchemeIsFile()) {
      media_player_->SetFileSource(fsl::CloneChannelFromFileDescriptor(
          fxl::UniqueFD(open(url.path().c_str(), O_RDONLY)).get()));
    } else {
      media_player_->SetHttpSource(params.url());
    }

    // Get the first frames queued up so we can show something.
    media_player_->Pause();
  }

  // These are for calculating frame rate.
  frame_time_ = media::Timeline::local_now();
  prev_frame_time_ = frame_time_;

  HandlePlayerStatusUpdates();
}

MediaPlayerView::~MediaPlayerView() {}

bool MediaPlayerView::OnInputEvent(input::InputEvent event) {
  bool handled = false;
  if (event.is_pointer()) {
    const auto& pointer = event.pointer();
    if (pointer.phase == input::PointerEventPhase::DOWN) {
      if (metadata_ && Contains(progress_bar_rect_, pointer.x, pointer.y)) {
        // User poked the progress bar...seek.
        media_player_->Seek((pointer.x - progress_bar_rect_.x) *
                            metadata_->duration / progress_bar_rect_.width);
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
    if (keyboard.phase == input::KeyboardEventPhase::PRESSED) {
      switch (keyboard.hid_usage) {
        case HID_USAGE_KEY_SPACE:
          TogglePlayPause();
          handled = true;
          break;
        case HID_USAGE_KEY_Q:
          loop_->Quit();
          handled = true;
          break;
        default:
          break;
      }
    }
  }
  return handled;
}

void MediaPlayerView::OnPropertiesChanged(
    views_v1::ViewProperties old_properties) {
  Layout();
}

void MediaPlayerView::Layout() {
  if (!has_logical_size())
    return;

  // Make the background fill the space.
  scenic_lib::Rectangle background_shape(session(), logical_size().width,
                                         logical_size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(logical_size().width * .5f,
                                  logical_size().height * .5f,
                                  kBackgroundElevation);

  // Compute maximum size of video content after reserving space
  // for decorations.
  geometry::SizeF max_content_size;
  max_content_size.width = logical_size().width - kMargin * 2;
  max_content_size.height =
      logical_size().height - kControlsHeight - kMargin * 3;

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

  // Add back in the decorations and center within view.
  geometry::RectF ui_rect;
  ui_rect.width = content_rect_.width;
  ui_rect.height = content_rect_.height + kControlsHeight + kMargin;
  ui_rect.x = (logical_size().width - ui_rect.width) / 2;
  ui_rect.y = (logical_size().height - ui_rect.height) / 2;

  // Position the video.
  content_rect_.x = ui_rect.x;
  content_rect_.y = ui_rect.y;

  // Position the controls.
  controls_rect_.x = content_rect_.x;
  controls_rect_.y = content_rect_.y + content_rect_.height + kMargin;
  controls_rect_.width = content_rect_.width;
  controls_rect_.height = kControlsHeight;

  // Position the progress bar (for input).
  progress_bar_rect_.x = controls_rect_.x + kSymbolWidth + kSymbolPadding * 2;
  progress_bar_rect_.y = controls_rect_.y;
  progress_bar_rect_.width =
      controls_rect_.width - (kSymbolWidth + kSymbolPadding * 2);
  progress_bar_rect_.height = controls_rect_.height;

  // Ask the view to fill the space.
  views_v1::ViewProperties view_properties;
  view_properties.view_layout = views_v1::ViewLayout::New();
  view_properties.view_layout->size.width = content_rect_.width;
  view_properties.view_layout->size.height = content_rect_.height;
  GetViewContainer()->SetChildProperties(
      kVideoChildKey, fidl::MakeOptional(std::move(view_properties)));

  InvalidateScene();
}

void MediaPlayerView::OnSceneInvalidated(
    images::PresentationInfo presentation_info) {
  if (!has_physical_size())
    return;

  prev_frame_time_ = frame_time_;
  frame_time_ = media::Timeline::local_now();

  // Log the frame rate every five seconds.
  if (state_ == State::kPlaying &&
      fxl::TimeDelta::FromNanoseconds(frame_time_).ToSeconds() / 5 !=
          fxl::TimeDelta::FromNanoseconds(prev_frame_time_).ToSeconds() / 5) {
    FXL_DLOG(INFO) << "frame rate " << frame_rate() << " fps";
  }

  // Position the video.
  if (video_host_node_) {
    video_host_node_->SetTranslation(content_rect_.x, content_rect_.y,
                                     kVideoElevation);
  }

  // Draw the progress bar.
  SkISize controls_size =
      SkISize::Make(controls_rect_.width, controls_rect_.height);
  SkCanvas* controls_canvas = controls_widget_.AcquireCanvas(
      controls_rect_.width, controls_rect_.height, metrics().scale_x,
      metrics().scale_y);
  DrawControls(controls_canvas, controls_size);
  controls_widget_.ReleaseAndSwapCanvas();
  controls_widget_.SetTranslation(
      controls_rect_.x + controls_rect_.width * .5f,
      controls_rect_.y + controls_rect_.height * .5f, kControlsElevation);

  // Animate the progress bar.
  if (state_ == State::kPlaying) {
    InvalidateScene();
  }
}

void MediaPlayerView::OnChildAttached(uint32_t child_key,
                                      views_v1::ViewInfo child_view_info) {
  FXL_DCHECK(child_key == kVideoChildKey);

  parent_node().AddChild(*video_host_node_);
  Layout();
}

void MediaPlayerView::OnChildUnavailable(uint32_t child_key) {
  FXL_DCHECK(child_key == kVideoChildKey);
  FXL_LOG(ERROR) << "Video view died unexpectedly";

  video_host_node_->Detach();
  video_host_node_.reset();

  GetViewContainer()->RemoveChild(child_key, nullptr);
  Layout();
}

void MediaPlayerView::DrawControls(SkCanvas* canvas, const SkISize& size) {
  canvas->clear(SK_ColorBLACK);

  // Draw the progress bar itself (blue on gray).
  float progress_bar_left = kSymbolWidth + kSymbolPadding * 2;
  float progress_bar_width = size.width() - progress_bar_left;
  SkPaint paint;
  paint.setColor(kProgressBarBackgroundColor);
  canvas->drawRect(
      SkRect::MakeXYWH(progress_bar_left, 0, progress_bar_width, size.height()),
      paint);

  paint.setColor(kProgressBarForegroundColor);
  canvas->drawRect(
      SkRect::MakeXYWH(progress_bar_left, 0, progress_bar_width * progress(),
                       size.height()),
      paint);

  paint.setColor(kProgressBarSymbolColor);
  float symbol_left = kSymbolPadding;
  float symbol_top = (size.height() - kSymbolHeight) / 2.0f;
  if (state_ == State::kPlaying) {
    // Playing...draw a pause symbol.
    canvas->drawRect(SkRect::MakeXYWH(symbol_left, symbol_top,
                                      kSymbolWidth / 3.0f, kSymbolHeight),
                     paint);

    canvas->drawRect(
        SkRect::MakeXYWH(symbol_left + 2 * kSymbolWidth / 3.0f, symbol_top,
                         kSymbolWidth / 3.0f, kSymbolHeight),
        paint);
  } else {
    // Playing...draw a play symbol.
    SkPath path;
    path.moveTo(symbol_left, symbol_top);
    path.lineTo(symbol_left, symbol_top + kSymbolHeight);
    path.lineTo(symbol_left + kSymbolWidth, symbol_top + kSymbolHeight / 2.0f);
    path.lineTo(symbol_left, symbol_top);
    canvas->drawPath(path, paint);
  }
}

void MediaPlayerView::HandlePlayerStatusUpdates(
    uint64_t version,
    media_player::MediaPlayerStatusPtr status) {
  if (status) {
    // Process status received from the player.
    if (status->timeline_transform) {
      timeline_function_ = media::TimelineFunction(*status->timeline_transform);
    }

    previous_state_ = state_;
    if (status->end_of_stream) {
      state_ = State::kEnded;
    } else if (timeline_function_.subject_delta() == 0) {
      state_ = State::kPaused;
    } else {
      state_ = State::kPlaying;
    }

    // TODO(dalesat): Display problems on the screen.
    if (status->problem) {
      if (!problem_shown_) {
        FXL_DLOG(INFO) << "PROBLEM: " << status->problem->type << ", "
                       << status->problem->details;
        problem_shown_ = true;
      }
    } else {
      problem_shown_ = false;
    }

    if (status->video_size && status->pixel_aspect_ratio &&
        (video_size_ != *status->video_size ||
         pixel_aspect_ratio_ != *status->pixel_aspect_ratio)) {
      video_size_ = *status->video_size;
      pixel_aspect_ratio_ = *status->pixel_aspect_ratio;

      FXL_LOG(INFO) << "video size " << status->video_size->width << "x"
                    << status->video_size->height << ", pixel aspect ratio "
                    << status->pixel_aspect_ratio->width << "x"
                    << status->pixel_aspect_ratio->height;

      Layout();
    }

    metadata_ = std::move(status->metadata);

    // TODO(dalesat): Display metadata on the screen.
    if (metadata_ && !metadata_shown_) {
      FXL_DLOG(INFO) << "duration   " << std::fixed << std::setprecision(1)
                     << double(metadata_->duration) / 1000000000.0
                     << " seconds";
      FXL_DLOG(INFO) << "title      "
                     << (metadata_->title ? metadata_->title : "<none>");
      FXL_DLOG(INFO) << "artist     "
                     << (metadata_->artist ? metadata_->artist : "<none>");
      FXL_DLOG(INFO) << "album      "
                     << (metadata_->album ? metadata_->album : "<none>");
      FXL_DLOG(INFO) << "publisher  "
                     << (metadata_->publisher ? metadata_->publisher
                                              : "<none>");
      FXL_DLOG(INFO) << "genre      "
                     << (metadata_->genre ? metadata_->genre : "<none>");
      FXL_DLOG(INFO) << "composer   "
                     << (metadata_->composer ? metadata_->composer : "<none>");
      metadata_shown_ = true;
    }

    // TODO(dalesat): Display frame rate on the screen.
  }

  InvalidateScene();

  // Request a status update.
  media_player_->GetStatus(
      version,
      [this](uint64_t version, media_player::MediaPlayerStatus status) {
        HandlePlayerStatusUpdates(version,
                                  fidl::MakeOptional(std::move(status)));
      });
}

void MediaPlayerView::TogglePlayPause() {
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

float MediaPlayerView::progress() const {
  if (!metadata_ || metadata_->duration == 0) {
    return 0.0f;
  }

  // Apply the timeline function to the current time.
  int64_t position = timeline_function_(media::Timeline::local_now());

  if (position < 0) {
    position = 0;
  }

  if (metadata_ && static_cast<uint64_t>(position) > metadata_->duration) {
    position = metadata_->duration;
  }

  return position / static_cast<float>(metadata_->duration);
}

}  // namespace examples
