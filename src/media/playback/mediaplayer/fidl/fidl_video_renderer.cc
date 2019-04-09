// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_video_renderer.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <limits>

#include "lib/media/timeline/timeline.h"
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace {

// TODO(dalesat): |ZX_RIGHT_WRITE| shouldn't be required, but it is.
static constexpr zx_rights_t kVmoDupRights =
    ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE |
    ZX_RIGHT_WRITE;

static constexpr uint32_t kBlackImageId = 1;
static constexpr uint32_t kBlackImageWidth = 2;
static constexpr uint32_t kBlackImageHeight = 2;
static const fuchsia::images::ImageInfo kBlackImageInfo{
    .width = kBlackImageWidth,
    .height = kBlackImageHeight,
    .stride = kBlackImageWidth * sizeof(uint32_t),
    .pixel_format = fuchsia::images::PixelFormat::BGRA_8};

}  // namespace

// static
std::shared_ptr<FidlVideoRenderer> FidlVideoRenderer::Create(
    component::StartupContext* startup_context) {
  return std::make_shared<FidlVideoRenderer>(startup_context);
}

FidlVideoRenderer::FidlVideoRenderer(component::StartupContext* startup_context)
    : startup_context_(startup_context),
      scenic_(startup_context_
                  ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>()),
      arrivals_(true) {
  supported_stream_types_.push_back(VideoStreamTypeSet::Create(
      {StreamType::kVideoEncodingUncompressed},
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max()),
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max())));

  // Create the black image VMO.
  size_t size =
      kBlackImageInfo.width * kBlackImageInfo.height * sizeof(uint32_t);
  fzl::VmoMapper mapper;
  zx_status_t status =
      mapper.CreateAndMap(size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                          &black_image_vmo_, kVmoDupRights);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "Failed to create and map VMO, status " << status;
  }

  memset(mapper.start(), 0, mapper.size());
}

FidlVideoRenderer::~FidlVideoRenderer() {}

const char* FidlVideoRenderer::label() const { return "video renderer"; }

void FidlVideoRenderer::Dump(std::ostream& os) const {
  Renderer::Dump(os);

  os << fostr::Indent;
  os << fostr::NewLine << "priming:               " << !!prime_callback_;
  os << fostr::NewLine << "flushed:               " << flushed_;
  os << fostr::NewLine << "flushing:              " << !!flush_callback_;
  os << fostr::NewLine << "presentation time:     "
     << AsNs(current_timeline_function()(media::Timeline::local_now()));
  os << fostr::NewLine << "video size:            " << video_size().width << "x"
     << video_size().height;
  os << fostr::NewLine
     << "pixel aspect ratio:    " << pixel_aspect_ratio().width << "x"
     << pixel_aspect_ratio().height;

  if (arrivals_.count() != 0) {
    os << fostr::NewLine << "video packet arrivals: " << fostr::Indent
       << arrivals_ << fostr::Outdent;
  }

  os << fostr::Outdent;
}

void FidlVideoRenderer::ConfigureConnectors() {
  // The decoder knows |max_payload_size|, so this is enough information to
  // configure the allocator(s).
  ConfigureInputToUseVmos(0,              // max_aggregate_payload_size
                          kPacketDemand,  // max_payload_count
                          0,              // max_payload_size
                          VmoAllocation::kVmoPerBuffer, false);
}

void FidlVideoRenderer::OnInputConnectionReady(size_t input_index) {
  FXL_DCHECK(input_index == 0);

  input_connection_ready_ = true;

  if (have_valid_image_info()) {
    UpdateImages();
  }
}

void FidlVideoRenderer::FlushInput(bool hold_frame, size_t input_index,
                                   fit::closure callback) {
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(callback);

  flushed_ = true;

  // TODO(dalesat): Cancel presentations on flush when that's supported.

  if (!hold_frame) {
    PresentBlackImage();
  }

  SetEndOfStreamPts(Packet::kNoPts);

  while (!packets_awaiting_presentation_.empty()) {
    packets_awaiting_presentation_.pop();
  }

  flush_callback_ = std::move(callback);
  flush_hold_frame_ = hold_frame;
  MaybeCompleteFlush();
}

void FidlVideoRenderer::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK(packet);
  FXL_DCHECK(input_index == 0);

  CheckForRevisedStreamType(packet);

  int64_t packet_pts_ns = packet->GetPts(media::TimelineRate::NsPerSecond);

  if (packet->end_of_stream()) {
    SetEndOfStreamPts(packet_pts_ns);

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  // Discard empty packets so they don't confuse the selection logic.
  // Discard packets that fall outside the program range.
  if (flushed_ || packet->payload() == nullptr || packet_pts_ns < min_pts(0) ||
      packet_pts_ns > max_pts(0)) {
    if (need_more_packets()) {
      RequestInputPacket();
    }

    return;
  }

  int64_t now = media::Timeline::local_now();

  arrivals_.AddSample(now, current_timeline_function()(now), packet_pts_ns,
                      Progressing());

  if (current_timeline_function().invertible()) {
    // We have a non-zero rate, so we can translate the packet PTS to system
    // time.
    PresentPacket(packet,
                  current_timeline_function().ApplyInverse(packet_pts_ns));
  } else {
    // The rate is zero, so we can't translate the packet's PTS to system
    // time.
    if (!initial_packet_presented_) {
      // No packet is currently being presented. We present this packet now,
      // so there's something to look at while we wait to progress.
      initial_packet_presented_ = true;
      PresentPacket(packet, now);
    } else {
      // Queue up the packet to be presented when we have a non-zero rate.
      packets_awaiting_presentation_.push(packet);
    }
  }

  if (need_more_packets()) {
    RequestInputPacket();
    return;
  }

  // We have enough packets. If we're priming, complete the operation.
  if (prime_callback_) {
    prime_callback_();
    prime_callback_ = nullptr;
  }
}

void FidlVideoRenderer::PresentPacket(PacketPtr packet,
                                      int64_t scenic_presentation_time) {
  // fbl::MakeRefCounted doesn't seem to forward these parameters properly, so
  // do it the old-fashioned way.
  auto release_tracker = fbl::AdoptRef(new ReleaseTracker(
      packet, std::static_pointer_cast<FidlVideoRenderer>(shared_from_this())));

  FXL_DCHECK(packet->payload_buffer()->vmo());
  uint32_t buffer_index = packet->payload_buffer()->vmo()->index();

  for (auto& [view_raw_ptr, view_unique_ptr] : views_) {
    // |PresentImage| will keep its reference to |release_tracker| until the
    // release fence is signalled or the |ImagePipe| connection closes.
    view_raw_ptr->PresentImage(buffer_index, scenic_presentation_time,
                               release_tracker, dispatcher());
  }

  ++presented_packets_not_released_;
}

void FidlVideoRenderer::SetStreamType(const StreamType& stream_type) {
  FXL_DCHECK(stream_type.medium() == StreamType::Medium::kVideo);
  FXL_DCHECK(stream_type.encoding() == StreamType::kVideoEncodingUncompressed);

  const VideoStreamType& video_stream_type = *stream_type.video();

  if (video_stream_type.pixel_format() ==
          VideoStreamType::PixelFormat::kUnknown ||
      video_stream_type.width() == 0 || video_stream_type.height() == 0) {
    // The decoder hasn't reported a real stream type yet.
    return;
  }

  bool had_valid_image_info = have_valid_image_info();

  // This really should be using |video_stream_type.width()| and
  // video_stream_type.height(). See the comment in |View::OnSceneInvalidated|
  // for more information.
  // TODO(dalesat): Change this once SCN-862 and SCN-141 are fixed.
  image_info_.width = video_stream_type.coded_width();
  image_info_.height = video_stream_type.coded_height();

  display_width_ = video_stream_type.width();
  display_height_ = video_stream_type.height();

  // TODO(dalesat): Stash these in image_info_ if/when those fields are added.
  pixel_aspect_ratio_.width = video_stream_type.pixel_aspect_ratio_width();
  pixel_aspect_ratio_.height = video_stream_type.pixel_aspect_ratio_height();

  image_info_.stride = video_stream_type.line_stride();

  switch (video_stream_type.pixel_format()) {
    case VideoStreamType::PixelFormat::kArgb:
      image_info_.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
      break;
    case VideoStreamType::PixelFormat::kYuy2:
      image_info_.pixel_format = fuchsia::images::PixelFormat::YUY2;
      break;
    case VideoStreamType::PixelFormat::kNv12:
      image_info_.pixel_format = fuchsia::images::PixelFormat::NV12;
      break;
    case VideoStreamType::PixelFormat::kYv12:
      image_info_.pixel_format = fuchsia::images::PixelFormat::YV12;
      break;
    default:
      // Not supported.
      // TODO(dalesat): Report the problem.
      FXL_LOG(FATAL) << "Unsupported pixel format.";
      break;
  }

  FXL_DCHECK(have_valid_image_info());

  if (!had_valid_image_info && input_connection_ready_) {
    // Updating images was deferred when |OnInputConnectionReady| was called,
    // because we didn't have a valid |ImageInfo|. Now we do, so...
    UpdateImages();
  }

  // We probably have new geometry, so invalidate the views.
  for (auto& [view_raw_ptr, view_unique_ptr] : views_) {
    view_raw_ptr->InvalidateScene();
  }
}

void FidlVideoRenderer::Prime(fit::closure callback) {
  flushed_ = false;

  if (presented_packets_not_released_ >= kPacketDemand ||
      end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = std::move(callback);
  RequestInputPacket();
}

fuchsia::math::Size FidlVideoRenderer::video_size() const {
  return fuchsia::math::Size{.width = static_cast<int32_t>(display_width_),
                             .height = static_cast<int32_t>(display_height_)};
}

fuchsia::math::Size FidlVideoRenderer::pixel_aspect_ratio() const {
  return pixel_aspect_ratio_;
}

void FidlVideoRenderer::SetGeometryUpdateCallback(fit::closure callback) {
  geometry_update_callback_ = std::move(callback);
}

void FidlVideoRenderer::CreateView(fuchsia::ui::views::ViewToken view_token) {
  scenic::ViewContext view_context{
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
      .view_token2 = std::move(view_token),
      .startup_context = startup_context_};
  auto view = std::make_unique<View>(
      std::move(view_context),
      std::static_pointer_cast<FidlVideoRenderer>(shared_from_this()));
  View* view_raw_ptr = view.get();
  views_.emplace(view_raw_ptr, std::move(view));

  view_raw_ptr->SetReleaseHandler(
      [this, view_raw_ptr](zx_status_t status) { views_.erase(view_raw_ptr); });

  view_raw_ptr->AddBlackImage(kBlackImageId, kBlackImageInfo, black_image_vmo_);

  if (have_valid_image_info() && input_connection_ready_) {
    // We're ready to add images to the new view, so do so.
    std::vector<fbl::RefPtr<PayloadVmo>> vmos = UseInputVmos().GetVmos();
    FXL_DCHECK(!vmos.empty());
    view_raw_ptr->UpdateImages(image_id_base_, image_info_, display_width_,
                               display_height_, vmos);
  }
}

void FidlVideoRenderer::UpdateImages() {
  std::vector<fbl::RefPtr<PayloadVmo>> vmos = UseInputVmos().GetVmos();
  FXL_DCHECK(!vmos.empty());

  if (vmos[0]->size() < image_info_.stride * image_info_.height) {
    // The payload VMOs are too small for the images. We will be getting a new
    // set of VMOs shortly, at which time |OnInputConnectionReady| will be
    // called, and we'll he here again with good VMOs.
    return;
  }

  image_id_base_ = next_image_id_base_;
  next_image_id_base_ = image_id_base_ + vmos.size();

  for (auto& [view_raw_ptr, view_unique_ptr] : views_) {
    view_raw_ptr->UpdateImages(image_id_base_, image_info_, display_width_,
                               display_height_, vmos);
  }
}

void FidlVideoRenderer::PresentBlackImage() {
  for (auto& [view_raw_ptr, view_unique_ptr] : views_) {
    view_raw_ptr->PresentBlackImage(kBlackImageId,
                                    media::Timeline::local_now());
  }
}

void FidlVideoRenderer::PacketReleased(PacketPtr packet) {
  --presented_packets_not_released_;

  MaybeCompleteFlush();

  if (need_more_packets()) {
    RequestInputPacket();
  }
}

void FidlVideoRenderer::MaybeCompleteFlush() {
  if (flush_callback_ &&
      (presented_packets_not_released_ <= flush_hold_frame_ ? 1 : 0)) {
    flush_callback_();
    flush_callback_ = nullptr;
  }
}

void FidlVideoRenderer::OnTimelineTransition() {
  if (!current_timeline_function().invertible()) {
    // The rate is zero, so we still can't present any images other than
    // the initial one.
    return;
  }

  while (!packets_awaiting_presentation_.empty()) {
    auto packet = packets_awaiting_presentation_.front();
    packets_awaiting_presentation_.pop();
    int64_t packet_pts_ns = packet->GetPts(media::TimelineRate::NsPerSecond);
    PresentPacket(packet,
                  current_timeline_function().ApplyInverse(packet_pts_ns));
  }

  if (need_more_packets()) {
    RequestInputPacket();
  }
}

void FidlVideoRenderer::CheckForRevisedStreamType(const PacketPtr& packet) {
  FXL_DCHECK(packet);

  const std::unique_ptr<StreamType>& revised_stream_type =
      packet->revised_stream_type();

  if (!revised_stream_type) {
    return;
  }

  if (revised_stream_type->medium() != StreamType::Medium::kVideo) {
    FXL_LOG(FATAL) << "Revised stream type was not video.";
  }

  FXL_DCHECK(revised_stream_type->video());

  SetStreamType(*revised_stream_type);

  if (geometry_update_callback_) {
    // Notify the player that geometry has changed. This eventually reaches
    // the parent view.
    geometry_update_callback_();
  }
}

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::ReleaseTracker implementation.

FidlVideoRenderer::ReleaseTracker::ReleaseTracker(
    PacketPtr packet, std::shared_ptr<FidlVideoRenderer> renderer)
    : packet_(packet), renderer_(renderer) {
  FXL_DCHECK(packet_);
  FXL_DCHECK(renderer_);
}

FidlVideoRenderer::ReleaseTracker::~ReleaseTracker() {
  renderer_->PacketReleased(packet_);
}

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::Image implementation.

FidlVideoRenderer::Image::Image()
    : wait_(this, ZX_HANDLE_INVALID, ZX_EVENT_SIGNALED) {}

void FidlVideoRenderer::Image::WaitHandler(async_dispatcher_t* dispatcher,
                                           async::WaitBase* wait,
                                           zx_status_t status,
                                           const zx_packet_signal_t* signal) {
  wait_.set_object(ZX_HANDLE_INVALID);
  release_fence_ = zx::event();

  // When this tracker is deleted, the renderer is informed that the image has
  // been released by all the image pipes that held it.
  release_tracker_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::View implementation.

FidlVideoRenderer::View::View(scenic::ViewContext context,
                              std::shared_ptr<FidlVideoRenderer> renderer)
    : scenic::BaseView(std::move(context), "Video Renderer"),
      renderer_(renderer),
      entity_node_(session()),
      clip_node_(session()),
      image_pipe_node_(session()),
      image_pipe_material_(session()) {
  FXL_DCHECK(renderer_);

  // Create an |ImagePipe|.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipeCmd(
      image_pipe_id, image_pipe_.NewRequest(renderer_->dispatcher())));

  image_pipe_.set_error_handler([this](zx_status_t status) {
    images_.reset();
    image_pipe_ = nullptr;
  });

  // Initialize |image_pipe_material_| so the image pipe is its texture.
  image_pipe_material_.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // |image_pipe_node_| will eventually be a rectangle that covers the entire
  // view, and will use |image_pipe_material_|. Unfortunately, an |ImagePipe|
  // texture that has no images is white, so in order to prevent a white
  // rectangle from flashing up during startup, we use a black material for
  // now.
  scenic::Material material(session());
  material.SetColor(0x00, 0x00, 0x00, 0xff);
  image_pipe_node_.SetMaterial(material);

  // Initialize |clip_node_|, which provides the geometry for |entity_node_|'s
  // clipping of |image_pipe_material_|. See the comment in
  // |OnSceneInvalidate| for why that's needed.
  scenic::Material clip_material(session());
  clip_material.SetColor(0x00, 0x00, 0x00, 0xff);
  clip_node_.SetMaterial(clip_material);

  // Connect the nodes up.
  entity_node_.AddPart(clip_node_);
  entity_node_.AddChild(image_pipe_node_);
  root_node().AddChild(entity_node_);

  // Turn clipping on. We specify clip-to-self here, which really means, clip-
  // to-part (|clip_node_|, in this case), evidently.
  entity_node_.SetClip(0, true);
}

FidlVideoRenderer::View::~View() {}

void FidlVideoRenderer::View::AddBlackImage(
    uint32_t image_id, fuchsia::images::ImageInfo image_info,
    const zx::vmo& vmo) {
  FXL_DCHECK(vmo);

  if (!image_pipe_) {
    FXL_LOG(FATAL) << "View::AddBlackImage called with no ImagePipe.";
    return;
  }

  zx::vmo duplicate;
  zx_status_t status = vmo.duplicate(kVmoDupRights, &duplicate);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "Failed to duplicate VMO, status " << status;
  }

  uint64_t size;
  status = vmo.get_size(&size);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "Failed to get size of VMO, status " << status;
  }

  // For now, we don't support non-zero memory offsets.
  image_pipe_->AddImage(image_id, image_info, std::move(duplicate),
                        /*offset_bytes=*/0, size,
                        fuchsia::images::MemoryType::HOST_MEMORY);
}

void FidlVideoRenderer::View::UpdateImages(
    uint32_t image_id_base, fuchsia::images::ImageInfo image_info,
    uint32_t display_width, uint32_t display_height,
    const std::vector<fbl::RefPtr<PayloadVmo>>& vmos) {
  FXL_DCHECK(!vmos.empty());

  if (!image_pipe_) {
    FXL_LOG(FATAL) << "View::UpdateImages called with no ImagePipe.";
    return;
  }

  image_width_ = image_info.width;
  image_height_ = image_info.height;
  display_width_ = display_width;
  display_height_ = display_height;

  // Remove the current set of images.
  for (auto& image : images_) {
    image_pipe_->RemoveImage(image.image_id_);
  }

  images_.reset(new Image[vmos.size()], vmos.size());

  uint32_t index = 0;
  for (auto vmo : vmos) {
    auto& image = images_[index];

    image.vmo_ = vmo;
    image.image_id_ = index + image_id_base;
    ++index;

    // For now, we don't support non-zero memory offsets.
    image_pipe_->AddImage(image.image_id_, image_info,
                          image.vmo_->Duplicate(kVmoDupRights),
                          /*offset_bytes=*/0, image.vmo_->size(),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }
}

void FidlVideoRenderer::View::PresentBlackImage(uint32_t image_id,
                                                uint64_t presentation_time) {
  if (!image_pipe_) {
    FXL_LOG(FATAL) << "View::PresentBlackImage called with no ImagePipe.";
    return;
  }

  image_pipe_->PresentImage(
      image_id, presentation_time, std::vector<zx::event>(),
      std::vector<zx::event>(),
      [](fuchsia::images::PresentationInfo presentation_info) {});
}

void FidlVideoRenderer::View::PresentImage(
    uint32_t buffer_index, uint64_t presentation_time,
    fbl::RefPtr<ReleaseTracker> release_tracker,
    async_dispatcher_t* dispatcher) {
  FXL_DCHECK(dispatcher);

  if (!image_pipe_) {
    FXL_LOG(FATAL) << "View::PresentImage called with no ImagePipe.";
    return;
  }

  FXL_DCHECK(buffer_index < images_.size());

  auto& image = images_[buffer_index];

  zx_status_t status = zx::event::create(0, &image.release_fence_);
  if (status != ZX_OK) {
    // The image won't get presented, but this is otherwise unharmful.
    // TODO(dalesat): Shut down playback and report the problem to the client.
    FXL_LOG(ERROR) << "Failed to create event in PresentImage.";
    return;
  }

  zx::event release_fence;
  status = image.release_fence_.duplicate(ZX_RIGHT_SIGNAL | ZX_RIGHTS_BASIC,
                                          &release_fence);
  if (status != ZX_OK) {
    // The image won't get presented, but this is otherwise unharmful.
    // TODO(dalesat): Shut down playback and report the problem to the client.
    FXL_LOG(ERROR) << "Failed to duplicate event in PresentImage.";
    image.release_fence_ = zx::event();
    return;
  }

  image.release_tracker_ = release_tracker;

  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));

  image.wait_.set_object(image.release_fence_.get());
  image.wait_.Begin(dispatcher);

  image_pipe_->PresentImage(
      image.image_id_, presentation_time, std::move(acquire_fences),
      std::move(release_fences),
      [](fuchsia::images::PresentationInfo presentation_info) {});
}

void FidlVideoRenderer::View::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Because scenic doesn't handle display height being different than image
  // height (SCN-862) or non-minimal stride (SCN-141), we have to position
  // |image_pipe_node_| so it extends beyond the view to the right and at the
  // bottom by |image_width_ - display_width_| and |image_height_ -
  // display_height_|, respectively, and clip off the pixels we don't want to
  // display. |entity_node_| does the clipping based on the geometry of
  // |clip_node_|.
  // TODO(dalesat): Remove this once SCN-862 and SCN-141 are fixed.
  image_pipe_node_.SetMaterial(image_pipe_material_);

  // Configure |clip_node_| to express the geometry of the clipping region.
  clip_node_.SetShape(
      scenic::Rectangle(session(), display_width_, display_height_));

  // Position |image_pipe_node_| so it overlaps the edge of |clip_node_| by
  // just enough to remove the parts we don't want to see.
  image_pipe_node_.SetShape(
      scenic::Rectangle(session(), image_width_, image_height_));
  image_pipe_node_.SetTranslation((image_width_ - display_width_) / 2.0f,
                                  (image_height_ - display_height_) / 2.0f,
                                  0.0f);

  // Scale |entity_node_| to fill the view.
  float width_scale = logical_size().x / display_width_;
  float height_scale = logical_size().y / display_height_;
  entity_node_.SetScale(width_scale, height_scale, 1.0f);

  // This |SetTranslation| shouldn't be necessary, but the flutter ChildView
  // widget doesn't take into account that scenic 0,0 is at center. As a
  // consequence, C++ parent views need to do this:
  //
  //    video_child_view->SetTranslation(video_rect.x, video_rect.y,
  //                                     kVideoElevation);
  //
  // instead of the normal thing, which would be this:
  //
  //    video_child_view->SetTranslation(
  //        video_rect.x + video_rect.width * 0.5f,
  //        video_rect.y + video_rect.height * 0.5f, kVideoElevation);
  //
  // TODO(dalesat): Remove this and update C++ parent views when SCN-1041 is
  // fixed.
  entity_node_.SetTranslation(logical_size().x * .5f, logical_size().y * .5f,
                              0.f);
}

}  // namespace media_player
