// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/media_player/media_player_view.h"

#include <hid/usages.h>

#include <iomanip>

#include "apps/media/examples/media_player/media_player_params.h"
#include "apps/media/lib/timeline.h"
#include "apps/media/services/audio_renderer.fidl.h"
#include "apps/media/services/audio_server.fidl.h"
#include "apps/media/services/media_service.fidl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace examples {

namespace {
constexpr uint32_t kVideoChildKey = 0u;

constexpr uint32_t kVideoResourceId = 1u;
constexpr uint32_t kControlsResourceId = 2u;

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kVideoNodeId = 1u;
constexpr uint32_t kControlsNodeId = 2u;

constexpr float kMargin = 4.0f;
constexpr float kControlsHeight = 36.0f;
constexpr float kSymbolWidth = 24.0f;
constexpr float kSymbolHeight = 24.0f;
constexpr float kSymbolPadding = 12.0f;

constexpr SkColor kProgressBarForegroundColor = 0xff673ab7;  // Deep Purple 500
constexpr SkColor kProgressBarBackgroundColor = 0xffb39ddb;  // Deep Purple 200
constexpr SkColor kProgressBarSymbolColor = 0xffffffff;

// Determines whether the rectangle contains the point x,y.
bool Contains(const mozart::RectF& rect, float x, float y) {
  return rect.x <= x && rect.y <= y && rect.x + rect.width >= x &&
         rect.y + rect.height >= y;
}
}  // namespace

MediaPlayerView::MediaPlayerView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    modular::ApplicationContext* application_context,
    const MediaPlayerParams& params)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Media Player"),
      input_handler_(GetViewServiceProvider(), this) {
  FTL_DCHECK(params.is_valid());
  FTL_DCHECK(!params.path().empty());

  media::MediaServicePtr media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();

  media::AudioServerPtr audio_service =
      application_context->ConnectToEnvironmentService<media::AudioServer>();

  // Get an audio renderer.
  media::AudioRendererPtr audio_renderer;
  media::MediaRendererPtr audio_media_renderer;
  audio_service->CreateRenderer(GetProxy(&audio_renderer),
                                GetProxy(&audio_media_renderer));

  // Get a video renderer.
  media::MediaRendererPtr video_media_renderer;
  media_service->CreateVideoRenderer(GetProxy(&video_renderer_),
                                     GetProxy(&video_media_renderer));

  mozart::ViewOwnerPtr video_view_owner;
  video_renderer_->CreateView(fidl::GetProxy(&video_view_owner));
  GetViewContainer()->AddChild(kVideoChildKey, std::move(video_view_owner));

  // We start with a non-zero size so we get a progress bar regardless of
  // whether we get video.
  video_size_.width = 640u;
  video_size_.height = 100u;
  video_renderer_->GetVideoSize([this](mozart::SizePtr video_size) {
    FTL_LOG(INFO) << "video_size " << video_size->width << "x"
                  << video_size->height;
    video_size_ = *video_size;
    Invalidate();
  });

  // Get a file reader.
  media::SeekingReaderPtr reader;
  media_service->CreateFileReader(params.path(), GetProxy(&reader));

  // Create a player from all that stuff.
  media_service->CreatePlayer(
      std::move(reader), std::move(audio_media_renderer),
      std::move(video_media_renderer), GetProxy(&media_player_));

  // Get the first frames queued up so we can show something.
  media_player_->Pause();

  // These are for calculating frame rate.
  frame_time_ = media::Timeline::local_now();
  prev_frame_time_ = frame_time_;

  HandleStatusUpdates();
}

MediaPlayerView::~MediaPlayerView() {}

void MediaPlayerView::OnEvent(mozart::EventPtr event,
                              const OnEventCallback& callback) {
  FTL_DCHECK(event);
  bool handled = false;
  switch (event->action) {
    case mozart::EventType::POINTER_DOWN:
      FTL_DCHECK(event->pointer_data);
      if (Contains(progress_bar_rect_, event->pointer_data->x,
                   event->pointer_data->y)) {
        // User poked the progress bar...seek.
        media_player_->Seek((event->pointer_data->x - progress_bar_rect_.x) *
                            metadata_->duration / progress_bar_rect_.width);
        if (state_ != State::kPlaying) {
          media_player_->Play();
        }
      } else {
        // User poked elsewhere.
        TogglePlayPause();
      }
      handled = true;
      break;

    case mozart::EventType::KEY_PRESSED:
      FTL_DCHECK(event->key_data);
      if (!event->key_data) {
        break;
      }
      switch (event->key_data->hid_usage) {
        case HID_USAGE_KEY_SPACE:
          TogglePlayPause();
          handled = true;
          break;
        case HID_USAGE_KEY_Q:
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
          handled = true;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  callback(handled);
}

void MediaPlayerView::OnLayout() {
  FTL_DCHECK(properties());

  auto view_properties = mozart::ViewProperties::New();
  view_properties->view_layout = mozart::ViewLayout::New();
  view_properties->view_layout->size = mozart::Size::New();
  view_properties->view_layout->size->width = video_size_.width;
  view_properties->view_layout->size->height = video_size_.height;

  if (video_view_properties_.Equals(view_properties)) {
    // no layout work to do
    return;
  }

  video_view_properties_ = view_properties.Clone();
  ++scene_version_;
  GetViewContainer()->SetChildProperties(kVideoChildKey, scene_version_,
                                         std::move(view_properties));
}

void MediaPlayerView::OnDraw() {
  FTL_DCHECK(properties());

  prev_frame_time_ = frame_time_;
  frame_time_ = media::Timeline::local_now();

  // Log the frame rate every five seconds.
  if (state_ == State::kPlaying &&
      ftl::TimeDelta::FromNanoseconds(frame_time_).ToSeconds() / 5 !=
          ftl::TimeDelta::FromNanoseconds(prev_frame_time_).ToSeconds() / 5) {
    FTL_DLOG(INFO) << "frame rate " << frame_rate() << " fps";
  }

  auto update = mozart::SceneUpdate::New();
  update->clear_nodes = true;
  update->clear_resources = true;

  const mozart::Size& view_size = *properties()->view_layout->size;

  if (view_size.width == 0 || view_size.height == 0) {
    // Nothing to show yet.
    update->nodes.insert(kRootNodeId, mozart::Node::New());
  } else {
    // Compute maximum size of video content after reserving space
    // for decorations.
    mozart::Size max_content_size;
    max_content_size.width = view_size.width - kMargin * 2;
    max_content_size.height = view_size.height - kControlsHeight - kMargin * 3;

    // Shrink video to fit if needed.
    mozart::Rect content_rect;
    if (max_content_size.width * video_size_.height <
        max_content_size.height * video_size_.width) {
      content_rect.width = max_content_size.width;
      content_rect.height =
          video_size_.height * max_content_size.width / video_size_.width;
    } else {
      content_rect.width =
          video_size_.width * max_content_size.height / video_size_.height;
      content_rect.height = max_content_size.height;
    }
    float content_scale =
        static_cast<float>(content_rect.width) / video_size_.width;

    // Add back in the decorations and center within view.
    mozart::Rect ui_rect;
    ui_rect.width = content_rect.width;
    ui_rect.height = content_rect.height + kControlsHeight + kMargin;
    ui_rect.x = (view_size.width - ui_rect.width) / 2;
    ui_rect.y = (view_size.height - ui_rect.height) / 2;

    // Position the video.
    content_rect.x = ui_rect.x;
    content_rect.y = ui_rect.y;

    // Position the controls.
    mozart::Rect controls_rect;
    controls_rect.x = content_rect.x;
    controls_rect.y = content_rect.y + content_rect.height + kMargin;
    controls_rect.width = content_rect.width;
    controls_rect.height = kControlsHeight;

    // Position the progress bar (for input).
    progress_bar_rect_.x = controls_rect.x + kSymbolWidth + kSymbolPadding * 2;
    progress_bar_rect_.y = controls_rect.y;
    progress_bar_rect_.width =
        controls_rect.width - (kSymbolWidth + kSymbolPadding * 2);
    progress_bar_rect_.height = controls_rect.height;

    // Create the root node.
    auto root = mozart::Node::New();

    // Draw the video.
    if (video_view_info_) {
      auto video_resource = mozart::Resource::New();
      video_resource->set_scene(mozart::SceneResource::New());
      video_resource->get_scene()->scene_token =
          video_view_info_->scene_token.Clone();
      update->resources.insert(kVideoResourceId, std::move(video_resource));

      auto video_node = mozart::Node::New();
      video_node->content_transform = mozart::Translate(
          mozart::CreateScaleTransform(content_scale, content_scale),
          content_rect.x, content_rect.y);
      video_node->op = mozart::NodeOp::New();
      video_node->op->set_scene(mozart::SceneNodeOp::New());
      video_node->op->get_scene()->scene_resource_id = kVideoResourceId;
      video_node->op->get_scene()->scene_version = scene_version_;
      update->nodes.insert(kVideoNodeId, std::move(video_node));

      root->child_node_ids.push_back(kVideoNodeId);
    }

    // Draw the progress bar.
    mozart::ImagePtr controls_image;
    SkISize controls_size =
        SkISize::Make(controls_rect.width, controls_rect.height);
    sk_sp<SkSurface> controls_surface = mozart::MakeSkSurface(
        controls_size, &buffer_producer_, &controls_image);
    FTL_CHECK(controls_surface);
    DrawControls(controls_surface->getCanvas(), controls_size);

    auto controls_resource = mozart::Resource::New();
    controls_resource->set_image(mozart::ImageResource::New());
    controls_resource->get_image()->image = std::move(controls_image);
    update->resources.insert(kControlsResourceId, std::move(controls_resource));

    auto controls_node = mozart::Node::New();
    controls_node->content_transform =
        mozart::CreateTranslationTransform(controls_rect.x, controls_rect.y);
    controls_node->op = mozart::NodeOp::New();
    controls_node->op->set_image(mozart::ImageNodeOp::New());
    controls_node->op->get_image()->content_rect = mozart::RectF::New();
    controls_node->op->get_image()->content_rect->x = 0;
    controls_node->op->get_image()->content_rect->y = 0;
    controls_node->op->get_image()->content_rect->width = controls_rect.width;
    controls_node->op->get_image()->content_rect->height = controls_rect.height;
    controls_node->op->get_image()->image_resource_id = kControlsResourceId;
    update->nodes.insert(kControlsNodeId, std::move(controls_node));

    root->child_node_ids.push_back(kControlsNodeId);

    // Finish up the root node.
    root->hit_test_behavior = mozart::HitTestBehavior::New();
    update->nodes.insert(kRootNodeId, std::move(root));
  }

  scene()->Update(std::move(update));
  scene()->Publish(CreateSceneMetadata());

  if (state_ == State::kPlaying) {
    // Need to animate the progress bar.
    Invalidate();
  }
}

void MediaPlayerView::OnChildAttached(uint32_t child_key,
                                      mozart::ViewInfoPtr child_view_info) {
  FTL_DCHECK(child_key == kVideoChildKey);

  video_view_info_ = std::move(child_view_info);
  Invalidate();
}

void MediaPlayerView::OnChildUnavailable(uint32_t child_key) {
  FTL_DCHECK(child_key == kVideoChildKey);
  FTL_LOG(ERROR) << "Video view died unexpectedly";

  video_view_info_.reset();

  GetViewContainer()->RemoveChild(child_key, nullptr);
  Invalidate();
}

void MediaPlayerView::DrawControls(SkCanvas* canvas, const SkISize& size) {
  canvas->clear(SK_ColorBLACK);

  // Draw the progress bar itself (blue on gray).
  float progress_bar_left = kSymbolWidth + kSymbolPadding * 2;
  float progress_bar_width = size.width() - kSymbolWidth - kSymbolPadding * 2;
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

void MediaPlayerView::HandleStatusUpdates(uint64_t version,
                                          media::MediaPlayerStatusPtr status) {
  if (status) {
    // Process status received from the player.
    if (status->timeline_transform) {
      timeline_function_ =
          status->timeline_transform.To<media::TimelineFunction>();
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
        FTL_DLOG(INFO) << "PROBLEM: " << status->problem->type << ", "
                       << status->problem->details;
        problem_shown_ = true;
      }
    } else {
      problem_shown_ = false;
    }

    metadata_ = std::move(status->metadata);

    // TODO(dalesat): Display metadata on the screen.
    if (metadata_ && !metadata_shown_) {
      FTL_DLOG(INFO) << "duration   " << std::fixed << std::setprecision(1)
                     << double(metadata_->duration) / 1000000000.0
                     << " seconds";
      FTL_DLOG(INFO) << "title      "
                     << (metadata_->title ? metadata_->title : "<none>");
      FTL_DLOG(INFO) << "artist     "
                     << (metadata_->artist ? metadata_->artist : "<none>");
      FTL_DLOG(INFO) << "album      "
                     << (metadata_->album ? metadata_->album : "<none>");
      FTL_DLOG(INFO) << "publisher  "
                     << (metadata_->publisher ? metadata_->publisher
                                              : "<none>");
      FTL_DLOG(INFO) << "genre      "
                     << (metadata_->genre ? metadata_->genre : "<none>");
      FTL_DLOG(INFO) << "composer   "
                     << (metadata_->composer ? metadata_->composer : "<none>");
      metadata_shown_ = true;
    }

    // TODO(dalesat): Display frame rate on the screen.
  }

  Invalidate();

  // Request a status update.
  media_player_->GetStatus(
      version, [this](uint64_t version, media::MediaPlayerStatusPtr status) {
        HandleStatusUpdates(version, std::move(status));
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
