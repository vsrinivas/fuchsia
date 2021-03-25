// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/fidl_video_renderer.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/zx/clock.h>

#include <limits>

#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"

namespace media_player {
namespace {

constexpr float kVideoElevation = 0.0f;

static constexpr uint32_t kVideoBufferCollectionId = 1;
static constexpr uint32_t kBlackImageBufferCollectionId = 2;
static constexpr uint32_t kBlackImageBufferIndex = 0;
static constexpr uint32_t kBlackImageId = 1;
static constexpr uint32_t kBlackImageWidth = 2;
static constexpr uint32_t kBlackImageHeight = 2;
static const fuchsia::sysmem::ImageFormat_2 kBlackImageFormat{
    .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8,
                     .has_format_modifier = false},
    .coded_width = kBlackImageWidth,
    .coded_height = kBlackImageHeight,
    .bytes_per_row = kBlackImageWidth * sizeof(uint32_t),
    .display_width = kBlackImageWidth,
    .display_height = kBlackImageHeight,
    .color_space = {.type = fuchsia::sysmem::ColorSpaceType::SRGB},
    .has_pixel_aspect_ratio = true,
    .pixel_aspect_ratio_width = 1,
    .pixel_aspect_ratio_height = 1};

}  // namespace

// static
std::shared_ptr<FidlVideoRenderer> FidlVideoRenderer::Create(
    sys::ComponentContext* component_context) {
  return std::make_shared<FidlVideoRenderer>(component_context);
}

FidlVideoRenderer::FidlVideoRenderer(sys::ComponentContext* component_context)
    : component_context_(component_context),
      scenic_(component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>()),
      arrivals_(true) {
  supported_stream_types_.push_back(
      VideoStreamTypeSet::Create({StreamType::kVideoEncodingUncompressed},
                                 Range<uint32_t>(0, std::numeric_limits<uint32_t>::max()),
                                 Range<uint32_t>(0, std::numeric_limits<uint32_t>::max())));

  AllocateBlackBuffer();
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
     << AsNs(current_timeline_function()(zx::clock::get_monotonic().get()));
  os << fostr::NewLine << "video size:            " << video_size().width << "x"
     << video_size().height;
  os << fostr::NewLine << "pixel aspect ratio:    " << pixel_aspect_ratio().width << "x"
     << pixel_aspect_ratio().height;

  if (arrivals_.count() != 0) {
    os << fostr::NewLine << "video packet arrivals: " << fostr::Indent << arrivals_
       << fostr::Outdent;
  }

  os << fostr::Outdent;
}

void FidlVideoRenderer::ConfigureConnectors() {
  // The decoder knows |max_payload_size|, so this is enough information to
  // configure the allocator(s).
  ConfigureInputToUseSysmemVmos(this,
                                0,                  // max_aggregate_payload_size
                                kPacketDemand - 1,  // max_payload_count
                                0,                  // max_payload_size
                                VmoAllocation::kVmoPerBuffer,
                                0);  // map_flags
}

void FidlVideoRenderer::OnInputConnectionReady(size_t input_index) {
  FX_DCHECK(input_index == 0);

  input_connection_ready_ = true;

  if (have_valid_image_format()) {
    UpdateImages();
  }
}

void FidlVideoRenderer::OnNewInputSysmemToken(size_t input_index) {
  FX_DCHECK(input_index == 0);

  if (view_) {
    view_->RemoveBufferCollection(kVideoBufferCollectionId);
    view_->AddBufferCollection(kVideoBufferCollectionId, TakeInputSysmemToken());
  }
}

void FidlVideoRenderer::FlushInput(bool hold_frame, size_t input_index, fit::closure callback) {
  FX_DCHECK(input_index == 0);
  FX_DCHECK(callback);

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
  FX_DCHECK(packet);
  FX_DCHECK(input_index == 0);

  CheckForRevisedStreamType(packet);

  int64_t packet_pts_ns = packet->GetPts(media::TimelineRate::NsPerSecond);

  if (!flushed_ && packet->end_of_stream()) {
    SetEndOfStreamPts(packet_pts_ns);

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  // Discard empty packets so they don't confuse the selection logic. We check the size rather than
  // seeing if the payload is null, because the payload will be null for non-empty payloads that
  // aren't mapped into local memory. Also discard packets that fall outside the program range.
  if (flushed_ || packet->size() == 0 || packet_pts_ns < min_pts(0) || packet_pts_ns > max_pts(0)) {
    if (packet->end_of_stream() && presented_packets_not_released_ <= 1) {
      // This is the end-of-stream packet, and it will not be presented,
      // probably because it has no payload. There is at most one packet
      // in the image pipe. No more packets will be released, because the last
      // packet is retained by the image pipe indefinitely. We update 'last
      // renderered pts' to the end-of-stream point so that end-of-stream will
      // be signalled. If there were more packets in the image pipe, this would
      // wait until all but that last one was released. See |PacketReleased|
      // below.
      UpdateLastRenderedPts(packet_pts_ns);
    }

    if (need_more_packets()) {
      RequestInputPacket();
    }

    return;
  }

  int64_t now = zx::clock::get_monotonic().get();

  arrivals_.AddSample(now, current_timeline_function()(now), packet_pts_ns, Progressing());

  if (current_timeline_function().invertible()) {
    // We have a non-zero rate, so we can translate the packet PTS to system
    // time.
    PresentPacket(packet, current_timeline_function().ApplyInverse(packet_pts_ns));
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

void FidlVideoRenderer::PresentPacket(PacketPtr packet, int64_t scenic_presentation_time) {
  // fbl::MakeRefCounted doesn't seem to forward these parameters properly, so
  // do it the old-fashioned way.
  auto release_tracker = fbl::AdoptRef(
      new ReleaseTracker(packet, std::static_pointer_cast<FidlVideoRenderer>(shared_from_this())));

  FX_DCHECK(packet->payload_buffer()->vmo());
  uint32_t buffer_index = packet->payload_buffer()->vmo()->index();

  FX_DCHECK(scenic_presentation_time >= prev_scenic_presentation_time_);

  if (view_) {
    if (packet->payload()) {
      zx_status_t status = zx_cache_flush(packet->payload(), packet->size(), ZX_CACHE_FLUSH_DATA);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Failed to flush payload";
      }
    }

    // |PresentImage| will keep its reference to |release_tracker| until the
    // release fence is signalled or the |ImagePipe| connection closes.
    view_->PresentImage(buffer_index, scenic_presentation_time, release_tracker, dispatcher());
  }

  prev_scenic_presentation_time_ = scenic_presentation_time;

  ++presented_packets_not_released_;
}

void FidlVideoRenderer::SetStreamType(const StreamType& stream_type) {
  FX_DCHECK(stream_type.medium() == StreamType::Medium::kVideo);
  FX_DCHECK(stream_type.encoding() == StreamType::kVideoEncodingUncompressed);

  const VideoStreamType& video_stream_type = *stream_type.video();

  if (video_stream_type.pixel_format() == VideoStreamType::PixelFormat::kUnknown ||
      video_stream_type.width() == 0 || video_stream_type.height() == 0) {
    // The decoder hasn't reported a real stream type yet.
    return;
  }

  bool had_valid_image_info = have_valid_image_format();

  // This really should be using |video_stream_type.width()| and
  // video_stream_type.height(). See the comment in |View::OnSceneInvalidated|
  // for more information.
  // TODO(dalesat): Change this once fxbug.dev/24079 and fxbug.dev/23396 are fixed.
  image_format_ = fidl::To<fuchsia::sysmem::ImageFormat_2>(video_stream_type);

  FX_DCHECK(have_valid_image_format());

  if (!had_valid_image_info && input_connection_ready_) {
    // Updating images was deferred when |OnInputConnectionReady| was called,
    // because we didn't have a valid |ImageInfo|. Now we do, so...
    UpdateImages();
  }

  // We probably have new geometry, so invalidate the view.
  if (view_) {
    view_->InvalidateScene();
  }
}

void FidlVideoRenderer::Prime(fit::closure callback) {
  flushed_ = false;

  if (presented_packets_not_released_ >= kPacketDemand || end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = std::move(callback);
  RequestInputPacket();
}

fuchsia::math::Size FidlVideoRenderer::video_size() const {
  return fuchsia::math::Size{.width = static_cast<int32_t>(image_format_.display_width),
                             .height = static_cast<int32_t>(image_format_.display_height)};
}

fuchsia::math::Size FidlVideoRenderer::pixel_aspect_ratio() const {
  return fuchsia::math::Size{
      .width = static_cast<int32_t>(image_format_.pixel_aspect_ratio_width),
      .height = static_cast<int32_t>(image_format_.pixel_aspect_ratio_height),
  };
}

void FidlVideoRenderer::ConnectToService(std::string service_path, zx::channel channel) {
  FX_DCHECK(component_context_);
  component_context_->svc()->Connect(service_path, std::move(channel));
}

void FidlVideoRenderer::SetGeometryUpdateCallback(fit::closure callback) {
  geometry_update_callback_ = std::move(callback);
}

void FidlVideoRenderer::CreateView(fuchsia::ui::views::ViewToken view_token) {
  scenic::ViewContext view_context{
      .session_and_listener_request =
          scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
      .view_token = std::move(view_token),
      .component_context = component_context_};
  view_ = std::make_unique<View>(std::move(view_context),
                                 std::static_pointer_cast<FidlVideoRenderer>(shared_from_this()));

  view_->SetReleaseHandler([this](zx_status_t status) { view_ = nullptr; });

  if (black_image_buffer_collection_token_for_pipe_) {
    // If we get here, |WaitForBuffersAllocated| is blocked in |AllocateBlackBuffer|. After the
    // image pipe gets this token, |WaitForBuffersAllocated| will return, and |AllocateBlackBuffer|
    // will add the black image.
    view_->AddBufferCollection(kBlackImageBufferCollectionId,
                               std::move(black_image_buffer_collection_token_for_pipe_));
  }

  // It's safe to call |TakeInputSysmemToken| here, because the player adds the renderer to the
  // graph before calling |CreateView|. |ConfigureConnectors| is called when the renderer is added
  // to the graph, and the token is available immediately after that.
  view_->AddBufferCollection(kVideoBufferCollectionId, TakeInputSysmemToken());

  if (have_valid_image_format() && input_connection_ready_) {
    // We're ready to add images to the new view, so do so.
    std::vector<fbl::RefPtr<PayloadVmo>> vmos = UseInputVmos().GetVmos();
    FX_DCHECK(!vmos.empty());
    view_->UpdateImages(image_id_base_, vmos.size(), kVideoBufferCollectionId, image_format_);
  }
}

void FidlVideoRenderer::AllocateBlackBuffer() {
  auto sysmem_allocator =
      static_cast<ServiceProvider*>(this)->ConnectToService<fuchsia::sysmem::Allocator>();
  sysmem_allocator->AllocateSharedCollection(black_image_buffer_collection_token_.NewRequest());
  black_image_buffer_collection_token_->Duplicate(
      ZX_DEFAULT_VMO_RIGHTS, black_image_buffer_collection_token_for_pipe_.NewRequest());

  if (view_) {
    FX_DCHECK(black_image_buffer_collection_);
    view_->AddBufferCollection(kBlackImageBufferCollectionId,
                               std::move(black_image_buffer_collection_token_for_pipe_));
  }

  black_image_buffer_collection_token_->Sync(
      [this, sysmem_allocator = std::move(sysmem_allocator)]() {
        sysmem_allocator->BindSharedCollection(std::move(black_image_buffer_collection_token_),
                                               black_image_buffer_collection_.NewRequest());

        uint64_t image_size =
            kBlackImageFormat.coded_width * kBlackImageFormat.coded_height * sizeof(uint32_t);

        // kBlackImageFormat
        fuchsia::sysmem::BufferCollectionConstraints constraints{
            .usage = fuchsia::sysmem::BufferUsage{.cpu = fuchsia::sysmem::cpuUsageRead |
                                                         fuchsia::sysmem::cpuUsageReadOften |
                                                         fuchsia::sysmem::cpuUsageWrite |
                                                         fuchsia::sysmem::cpuUsageWriteOften},
            .min_buffer_count_for_camping = 0,
            .min_buffer_count_for_dedicated_slack = 0,
            .min_buffer_count_for_shared_slack = 0,
            .min_buffer_count = 1,
            .max_buffer_count = 0,
            .has_buffer_memory_constraints = true,
            .image_format_constraints_count = 1};

        constraints.buffer_memory_constraints.min_size_bytes = image_size;
        constraints.buffer_memory_constraints.heap_permitted_count = 0;
        constraints.buffer_memory_constraints.ram_domain_supported = true;

        auto& image_constraints = constraints.image_format_constraints[0];
        image_constraints.pixel_format = kBlackImageFormat.pixel_format;
        image_constraints.color_spaces_count = 1;
        image_constraints.color_space[0] = kBlackImageFormat.color_space;
        image_constraints.required_min_coded_width = kBlackImageFormat.coded_width;
        image_constraints.required_max_coded_width = kBlackImageFormat.coded_width;
        image_constraints.required_min_coded_height = kBlackImageFormat.coded_height;
        image_constraints.required_max_coded_height = kBlackImageFormat.coded_height;

        black_image_buffer_collection_->SetConstraints(true, constraints);

        // If there is no view at the moment, this method will hang until |CreateView| is
        // called, after to which, we'll add the image to the view.
        black_image_buffer_collection_->WaitForBuffersAllocated(
            [this, image_size](zx_status_t status,
                               fuchsia::sysmem::BufferCollectionInfo_2 collection_info) {
              if (status != ZX_OK) {
                FX_PLOGS(ERROR, status) << "Sysmem buffer allocation failed for black image";
                return;
              }

              FX_DCHECK(collection_info.buffer_count > 0);

              auto& vmo_buffer = collection_info.buffers[0];
              FX_DCHECK(vmo_buffer.vmo_usable_start == 0);
              FX_CHECK(vmo_buffer.vmo.is_valid());

              fzl::VmoMapper mapper;
              status = mapper.Map(vmo_buffer.vmo, 0, image_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                  nullptr);
              if (status != ZX_OK) {
                FX_PLOGS(ERROR, status) << "Failed to map VMO";
                return;
              }

              memset(mapper.start(), 0, mapper.size());

              if (view_) {
                FX_DCHECK(black_image_buffer_collection_);
                view_->AddBlackImage(kBlackImageId, kBlackImageBufferCollectionId,
                                     kBlackImageBufferIndex, kBlackImageFormat);
              }
            });
      });
}

void FidlVideoRenderer::UpdateImages() {
  std::vector<fbl::RefPtr<PayloadVmo>> vmos = UseInputVmos().GetVmos();
  FX_DCHECK(!vmos.empty());

  if (vmos[0]->size() < image_format_.bytes_per_row * image_format_.coded_height) {
    // The payload VMOs are too small for the images. We will be getting a new
    // set of VMOs shortly, at which time |OnInputConnectionReady| will be
    // called, and we'll he here again with good VMOs.
    return;
  }

  image_id_base_ = next_image_id_base_;
  next_image_id_base_ = image_id_base_ + vmos.size();

  if (view_) {
    view_->UpdateImages(image_id_base_, vmos.size(), kVideoBufferCollectionId, image_format_);
  }
}

void FidlVideoRenderer::PresentBlackImage() {
  if (view_) {
    view_->PresentBlackImage(kBlackImageId, prev_scenic_presentation_time_);
  }
}

void FidlVideoRenderer::PacketReleased(PacketPtr packet) {
  --presented_packets_not_released_;

  if (end_of_stream_pending() && presented_packets_not_released_ == 1) {
    // End-of-stream is pending, and all packets except the last one have been
    // released. We update 'last renderered pts' to the end-of-stream point
    // assuming that the last packet is now being presented by the image pipe.
    // This logic is required, because the last packet is retained by the image
    // pipe indefinitely.
    UpdateLastRenderedPts(end_of_stream_pts());
  } else {
    // Indicate that the released packet has been rendered.
    UpdateLastRenderedPts(packet->GetPts(media::TimelineRate::NsPerSecond));
  }

  MaybeCompleteFlush();

  if (need_more_packets()) {
    RequestInputPacket();
  }
}

void FidlVideoRenderer::MaybeCompleteFlush() {
  if (flush_callback_ && (presented_packets_not_released_ <= flush_hold_frame_ ? 1 : 0)) {
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
    PresentPacket(packet, current_timeline_function().ApplyInverse(packet_pts_ns));
  }

  if (need_more_packets()) {
    RequestInputPacket();
  }
}

void FidlVideoRenderer::CheckForRevisedStreamType(const PacketPtr& packet) {
  FX_DCHECK(packet);

  auto revised_stream_type = packet->revised_stream_type();
  if (!revised_stream_type) {
    return;
  }

  if (revised_stream_type->medium() != StreamType::Medium::kVideo) {
    FX_LOGS(FATAL) << "Revised stream type was not video.";
  }

  FX_DCHECK(revised_stream_type->video());

  SetStreamType(*revised_stream_type);

  if (geometry_update_callback_) {
    // Notify the player that geometry has changed. This eventually reaches
    // the parent view.
    geometry_update_callback_();
  }
}

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::ReleaseTracker implementation.

FidlVideoRenderer::ReleaseTracker::ReleaseTracker(PacketPtr packet,
                                                  std::shared_ptr<FidlVideoRenderer> renderer)
    : packet_(packet), renderer_(renderer) {
  FX_DCHECK(packet_);
  FX_DCHECK(renderer_);
}

FidlVideoRenderer::ReleaseTracker::~ReleaseTracker() { renderer_->PacketReleased(packet_); }

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::Image implementation.

FidlVideoRenderer::Image::Image() : wait_(this, ZX_HANDLE_INVALID, ZX_EVENT_SIGNALED) {}

void FidlVideoRenderer::Image::WaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
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
      image_pipe_node_(session()),
      image_pipe_material_(session()) {
  FX_DCHECK(renderer_);

  // Create an |ImagePipe|.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipe2Cmd(
      image_pipe_id, image_pipe_.NewRequest(renderer_->dispatcher())));

  image_pipe_.set_error_handler([this](zx_status_t status) {
    images_ = nullptr;
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

  // Connect the nodes up.
  entity_node_.AddChild(image_pipe_node_);
  root_node().AddChild(entity_node_);
}

FidlVideoRenderer::View::~View() {}

void FidlVideoRenderer::View::AddBufferCollection(uint32_t buffer_collection_id,
                                                  fuchsia::sysmem::BufferCollectionTokenPtr token) {
  if (!image_pipe_) {
    FX_LOGS(ERROR) << "View::AddBufferCollection called with no ImagePipe.";
    return;
  }

  image_pipe_->AddBufferCollection(buffer_collection_id, std::move(token));
}

void FidlVideoRenderer::View::RemoveBufferCollection(uint32_t buffer_collection_id) {
  if (!image_pipe_) {
    FX_LOGS(ERROR) << "View::RemoveBufferCollection called with no ImagePipe.";
    return;
  }

  image_pipe_->RemoveBufferCollection(buffer_collection_id);
}

void FidlVideoRenderer::View::AddBlackImage(uint32_t image_id, uint32_t buffer_collection_id,
                                            uint32_t buffer_index,
                                            fuchsia::sysmem::ImageFormat_2 image_format) {
  if (!image_pipe_) {
    FX_LOGS(ERROR) << "View::AddBlackImage called with no ImagePipe.";
    return;
  }

  if (black_image_added_) {
    return;
  }

  image_pipe_->AddImage(image_id, buffer_collection_id, buffer_index, image_format);
  black_image_added_ = true;
}

void FidlVideoRenderer::View::UpdateImages(uint32_t image_id_base, uint32_t image_count,
                                           uint32_t buffer_collection_id,
                                           fuchsia::sysmem::ImageFormat_2 image_format) {
  FX_DCHECK(image_count != 0);

  if (!image_pipe_) {
    FX_LOGS(FATAL) << "View::UpdateImages called with no ImagePipe.";
    return;
  }

  image_width_ = image_format.coded_width;
  image_height_ = image_format.coded_height;
  display_width_ = image_format.display_width;
  display_height_ = image_format.display_height;

  // We never need to |RemoveImage|, because we |RemoveBufferCollection|,
  // which causes the images to be removed.

  images_.reset(new Image[image_count], image_count);

  for (uint32_t index = 0; index < image_count; ++index) {
    auto& image = images_[index];

    image.buffer_index_ = index;
    image.image_id_ = index + image_id_base;

    // For now, we don't support non-zero memory offsets.
    image_pipe_->AddImage(image.image_id_, buffer_collection_id, image.buffer_index_, image_format);
  }
}

void FidlVideoRenderer::View::PresentBlackImage(uint32_t image_id, uint64_t presentation_time) {
  if (!image_pipe_) {
    FX_LOGS(FATAL) << "View::PresentBlackImage called with no ImagePipe.";
    return;
  }

  if (!black_image_added_) {
    // We haven't added the black image yet, so we can't present it.
    FX_LOGS(WARNING) << "View::PresentBlackImage black image not added yet";
    return;
  }

  image_pipe_->PresentImage(image_id, presentation_time, std::vector<zx::event>(),
                            std::vector<zx::event>(),
                            [](fuchsia::images::PresentationInfo presentation_info) {});
}

void FidlVideoRenderer::View::PresentImage(uint32_t buffer_index, uint64_t presentation_time,
                                           fbl::RefPtr<ReleaseTracker> release_tracker,
                                           async_dispatcher_t* dispatcher) {
  FX_DCHECK(dispatcher);

  if (!image_pipe_) {
    FX_LOGS(FATAL) << "View::PresentImage called with no ImagePipe.";
    return;
  }

  FX_DCHECK(buffer_index < images_.size());

  auto& image = images_[buffer_index];

  zx_status_t status = zx::event::create(0, &image.release_fence_);
  if (status != ZX_OK) {
    // The image won't get presented, but this is otherwise unharmful.
    // TODO(dalesat): Shut down playback and report the problem to the client.
    FX_LOGS(ERROR) << "Failed to create event in PresentImage.";
    return;
  }

  zx::event release_fence;
  status = image.release_fence_.duplicate(ZX_RIGHT_SIGNAL | ZX_RIGHTS_BASIC, &release_fence);
  if (status != ZX_OK) {
    // The image won't get presented, but this is otherwise unharmful.
    // TODO(dalesat): Shut down playback and report the problem to the client.
    FX_LOGS(ERROR) << "Failed to duplicate event in PresentImage.";
    image.release_fence_ = zx::event();
    return;
  }

  image.release_tracker_ = release_tracker;

  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));

  image.wait_.set_object(image.release_fence_.get());
  image.wait_.Begin(dispatcher);

  image_pipe_->PresentImage(image.image_id_, presentation_time, std::move(acquire_fences),
                            std::move(release_fences),
                            [](fuchsia::images::PresentationInfo presentation_info) {});
}

void FidlVideoRenderer::View::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size() || display_width_ == 0 || display_height_ == 0) {
    return;
  }

  image_pipe_node_.SetMaterial(image_pipe_material_);

  image_pipe_node_.SetShape(scenic::Rectangle(session(), display_width_, display_height_));
  image_pipe_node_.SetTranslation(0.0f, 0.0f, kVideoElevation);

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
  // TODO(dalesat): Remove this and update C++ parent views when fxbug.dev/24252 is
  // fixed.
  entity_node_.SetTranslation(logical_size().x * .5f, logical_size().y * .5f, 0.0f);
}

}  // namespace media_player
