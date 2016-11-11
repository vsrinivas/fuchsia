// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/examples/video_player/video_player_view.h"

#include <hid/usages.h>

#include <iomanip>

#include "apps/media/cpp/timeline.h"
#include "apps/media/examples/video_player/video_player_params.h"
#include "apps/media/services/audio_server.fidl.h"
#include "apps/media/services/audio_track.fidl.h"
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
constexpr uint32_t kSkiaImageResourceId = 1;
constexpr uint32_t kVideoImageResourceId = 2;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;

// Creates a RectF at the origin with the specified size.
mozart::RectFPtr CreateRectF(const mozart::Size& size) {
  auto rectF = mozart::RectF::New();
  rectF->x = 0.0f;
  rectF->y = 0.0f;
  rectF->width = size.width;
  rectF->height = size.height;
  return rectF;
}

// Determines whether the rectangle contains the point x,y.
bool Contains(const mozart::RectF& rect, float x, float y) {
  return rect.x <= x && rect.y <= y && rect.x + rect.width >= x &&
         rect.y + rect.height >= y;
}
}  // namespace

VideoPlayerView::VideoPlayerView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    modular::ApplicationContext* application_context,
    const VideoPlayerParams& params)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Video Player"),
      input_handler_(GetViewServiceProvider(), this) {
  FTL_DCHECK(params.is_valid());
  FTL_DCHECK(!params.path().empty());

  media::MediaServicePtr media_service =
      application_context->ConnectToEnvironmentService<media::MediaService>();

  media::AudioServerPtr audio_service =
      application_context->ConnectToEnvironmentService<media::AudioServer>();

  // Get an audio renderer.
  media::AudioTrackPtr audio_track;
  media::MediaRendererPtr audio_renderer;
  audio_service->CreateTrack(GetProxy(&audio_track), GetProxy(&audio_renderer));

  // Get a video renderer (in-proc for now).
  media::MediaRendererPtr video_renderer;
  fidl::InterfaceRequest<media::MediaRenderer> video_renderer_request =
      fidl::GetProxy(&video_renderer);
  video_renderer_.Bind(std::move(video_renderer_request));

  // Get a file reader.
  media::SeekingReaderPtr reader;
  media_service->CreateFileReader(params.path(), GetProxy(&reader));

  // Create a player from all that stuff.
  media_service->CreatePlayer(
      std::move(reader), nullptr,  // std::move(audio_renderer),
      std::move(video_renderer), GetProxy(&media_player_));

  // Get the first frames queued up so we can show something.
  media_player_->Pause();

  // These are for calculating frame rate.
  frame_time_ = media::Timeline::local_now();
  prev_frame_time_ = frame_time_;

  HandleStatusUpdates();
}

VideoPlayerView::~VideoPlayerView() {}

void VideoPlayerView::OnEvent(mozart::EventPtr event,
                              const OnEventCallback& callback) {
  FTL_DCHECK(event);
  bool handled = false;
  switch (event->action) {
    case mozart::EventType::POINTER_DOWN:
      FTL_DCHECK(event->pointer_data);
      if (Contains(progress_bar_rect_,
                   event->pointer_data->x + progress_bar_rect_.x,
                   event->pointer_data->y)) {
        // User poked the progress bar...seek.
        media_player_->Seek(event->pointer_data->x * metadata_->duration /
                            progress_bar_rect_.width);
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

void VideoPlayerView::OnDraw() {
  FTL_DCHECK(properties());

  prev_frame_time_ = frame_time_;
  frame_time_ = media::Timeline::local_now();

  // Log the frame rate every five seconds.
  if (ftl::TimeDelta::FromNanoseconds(frame_time_).ToSeconds() / 5 !=
      ftl::TimeDelta::FromNanoseconds(prev_frame_time_).ToSeconds() / 5) {
    FTL_DLOG(INFO) << "frame rate " << frame_rate() << " fps";
  }

  auto update = mozart::SceneUpdate::New();

  const mozart::Size& view_size = *properties()->view_layout->size;
  mozart::Size video_size = video_renderer_.GetSize();

  if (view_size.width == 0 || view_size.height == 0 || video_size.width == 0 ||
      video_size.height == 0) {
    // Nothing to show yet.
    update->nodes.insert(kRootNodeId, mozart::Node::New());
  } else {
    auto children = fidl::Array<uint32_t>::New(0);

    // Shrink-to-fit the video horizontally, if necessary, otherwise center it.
    float width_scale = static_cast<float>(view_size.width) /
                        static_cast<float>(video_size.width);
    float height_scale = static_cast<float>(view_size.height) /
                         static_cast<float>(video_size.height);
    float scale = std::min(width_scale, height_scale);
    float translate_x = 0.0f;

    if (scale > 1.0f) {
      scale = 1.0f;
      translate_x = (view_size.width - video_size.width) / 2.0f;
    }

    mozart::TransformPtr transform = mozart::Translate(
        mozart::CreateScaleTransform(scale, scale), translate_x, kMargin);

    // Use the transform to position the progress bar under the video.
    mozart::PointF progress_bar_left;
    progress_bar_left.x = 0.0f;
    progress_bar_left.y = video_size.height;
    progress_bar_left = TransformPoint(*transform, progress_bar_left);

    mozart::PointF progress_bar_right;
    progress_bar_right.x = video_size.width;
    progress_bar_right.y = video_size.height;
    progress_bar_right = TransformPoint(*transform, progress_bar_right);

    progress_bar_rect_.x = progress_bar_left.x;
    progress_bar_rect_.y = progress_bar_left.y + kMargin;
    progress_bar_rect_.width = progress_bar_right.x - progress_bar_left.x;
    progress_bar_rect_.height = kProgressBarHeight;

    // Create the image node and apply the transform to it to scale and
    // position it properly.
    auto video_node = MakeVideoNode(std::move(transform), update);
    update->nodes.insert(children.size() + 1, std::move(video_node));
    children.push_back(children.size() + 1);

    // Create a node in which to do skia drawing.
    mozart::RectF skia_rect = progress_bar_rect_;
    skia_rect.height =
        kProgressBarHeight + kSymbolVerticalSpacing + kSymbolHeight;

    update->nodes.insert(
        children.size() + 1,
        MakeSkiaNode(kSkiaImageResourceId, skia_rect,
                     [this](const mozart::Size& size, SkCanvas* canvas) {
                       DrawSkiaContent(size, canvas);
                     },
                     update));
    children.push_back(children.size() + 1);

    // Create the root node.
    auto root = mozart::Node::New();
    root->child_node_ids = std::move(children);
    update->nodes.insert(kRootNodeId, std::move(root));
  }

  scene()->Update(std::move(update));
  scene()->Publish(CreateSceneMetadata());

  // Draw again immediately. It would be great to do this only when we're
  // playing, but we aren't told when the video renderer get its first frame,
  // so we might miss showing the first frame on startup. When the video
  // renderer is implemented as a ViewProvider, we won't need to animate like
  // this anymore.
  Invalidate();
}

mozart::NodePtr VideoPlayerView::MakeSkiaNode(
    uint32_t resource_id,
    const mozart::RectF rect,
    const std::function<void(const mozart::Size&, SkCanvas*)> content_drawer,
    const mozart::SceneUpdatePtr& update) {
  FTL_DCHECK(update);

  mozart::Size size;
  size.width = rect.width;
  size.height = rect.height;

  mozart::ImagePtr image;
  sk_sp<SkSurface> surface = mozart::MakeSkSurface(size, &image);
  FTL_DCHECK(surface);
  content_drawer(size, surface->getCanvas());
  auto content_resource = mozart::Resource::New();
  content_resource->set_image(mozart::ImageResource::New());
  content_resource->get_image()->image = std::move(image);
  update->resources.insert(resource_id, std::move(content_resource));

  auto skia_node = mozart::Node::New();
  skia_node->content_transform =
      mozart::CreateTranslationTransform(rect.x, rect.y);
  skia_node->op = mozart::NodeOp::New();
  skia_node->op->set_image(mozart::ImageNodeOp::New());
  skia_node->op->get_image()->content_rect = CreateRectF(size);
  skia_node->op->get_image()->image_resource_id = resource_id;

  return skia_node;
}

void VideoPlayerView::DrawSkiaContent(const mozart::Size& size,
                                      SkCanvas* canvas) {
  canvas->clear(SK_ColorBLACK);

  // Draw the progress bar (blue on gray).
  SkPaint paint;
  paint.setColor(kColorGray);
  canvas->drawRect(SkRect::MakeWH(size.width, kProgressBarHeight), paint);

  paint.setColor(kColorBlue);
  canvas->drawRect(SkRect::MakeWH(size.width * progress(), kProgressBarHeight),
                   paint);

  paint.setColor(kColorGray);

  if (state_ == State::kPlaying) {
    // Playing...draw a pause symbol.
    canvas->drawRect(
        SkRect::MakeXYWH((size.width - kSymbolWidth) / 2.0f,
                         kProgressBarHeight + kSymbolVerticalSpacing,
                         kSymbolWidth / 3.0f, kSymbolHeight),
        paint);

    canvas->drawRect(
        SkRect::MakeXYWH((size.width + kSymbolWidth / 3.0f) / 2.0f,
                         kProgressBarHeight + kSymbolVerticalSpacing,
                         kSymbolWidth / 3.0f, kSymbolHeight),
        paint);
  } else {
    // Playing...draw a play symbol.
    SkPath path;
    path.moveTo((size.width - kSymbolWidth) / 2.0f,
                kProgressBarHeight + kSymbolVerticalSpacing);
    path.lineTo((size.width - kSymbolWidth) / 2.0f,
                kProgressBarHeight + kSymbolVerticalSpacing + kSymbolHeight);
    path.lineTo(
        (size.width - kSymbolWidth) / 2.0f + kSymbolWidth,
        kProgressBarHeight + kSymbolVerticalSpacing + kSymbolHeight / 2);
    path.lineTo((size.width - kSymbolWidth) / 2.0f,
                kProgressBarHeight + kSymbolVerticalSpacing);
    canvas->drawPath(path, paint);
  }
}

mozart::NodePtr VideoPlayerView::MakeVideoNode(
    mozart::TransformPtr transform,
    const mozart::SceneUpdatePtr& update) {
  mozart::Size video_size = video_renderer_.GetSize();

  if (video_size.width == 0 || video_size.height == 0) {
    return mozart::Node::New();
  }

  mozart::ResourcePtr vid_resource = DrawVideoTexture(
      video_size, frame_tracker().frame_info().presentation_time);
  FTL_DCHECK(vid_resource);
  update->resources.insert(kVideoImageResourceId, std::move(vid_resource));

  auto video_node = mozart::Node::New();
  video_node->content_transform = std::move(transform);
  video_node->hit_test_behavior = mozart::HitTestBehavior::New();
  video_node->op = mozart::NodeOp::New();
  video_node->op->set_image(mozart::ImageNodeOp::New());
  video_node->op->get_image()->content_rect = CreateRectF(video_size);
  video_node->op->get_image()->image_resource_id = kVideoImageResourceId;

  return video_node;
}

mozart::ResourcePtr VideoPlayerView::DrawVideoTexture(
    const mozart::Size& size,
    int64_t presentation_time) {
  EnsureBuffer(size);

  mozart::ImagePtr image = mozart::Image::New();
  image->size = size.Clone();
  image->stride = size.width * sizeof(uint32_t);
  image->pixel_format = mozart::Image::PixelFormat::B8G8R8A8;
  image->alpha_format = mozart::Image::AlphaFormat::OPAQUE;
  image->buffer = buffer_.GetDuplicateVmo();

  video_renderer_.GetRgbaFrame(static_cast<uint8_t*>(buffer_.PtrFromOffset(0)),
                               size, presentation_time);

  mozart::ResourcePtr resource = mozart::Resource::New();
  resource->set_image(mozart::ImageResource::New());
  resource->get_image()->image = std::move(image);
  return resource;
}

void VideoPlayerView::EnsureBuffer(const mozart::Size& size) {
  if (!buffer_.initialized() || buffer_size_ != size) {
    buffer_.Reset();
    mx_status_t status =
        buffer_.InitNew(size.height * size.width * sizeof(uint32_t));
    FTL_DCHECK(status == NO_ERROR);
    buffer_size_ = size;
    std::memset(buffer_.PtrFromOffset(0), 0, buffer_.size());
  }
}

void VideoPlayerView::HandleStatusUpdates(uint64_t version,
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

void VideoPlayerView::TogglePlayPause() {
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

float VideoPlayerView::progress() const {
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
