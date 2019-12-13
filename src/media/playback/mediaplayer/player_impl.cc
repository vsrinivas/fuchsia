// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/player_impl.h"

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fidl/cpp/type_converter.h>
#include <lib/fit/function.h>
#include <lib/media/cpp/type_converters.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/zx/clock.h>

#include <sstream>

#include <fs/pseudo_file.h>

#include "src/lib/ui/base_view/base_view.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/playback/mediaplayer/core/demux_source_segment.h"
#include "src/media/playback/mediaplayer/core/renderer_sink_segment.h"
#include "src/media/playback/mediaplayer/demux/file_reader.h"
#include "src/media/playback/mediaplayer/demux/reader_cache.h"
#include "src/media/playback/mediaplayer/fidl/fidl_audio_renderer.h"
#include "src/media/playback/mediaplayer/fidl/fidl_reader.h"
#include "src/media/playback/mediaplayer/fidl/fidl_type_conversions.h"
#include "src/media/playback/mediaplayer/fidl/fidl_video_renderer.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/thread_priority.h"
#include "src/media/playback/mediaplayer/source_impl.h"
#include "src/media/playback/mediaplayer/util/safe_clone.h"

namespace media_player {
namespace {

static const char* kDumpEntry = "dump";

// TODO(turnage): Choose these based on media type or expose them to clients.
static constexpr zx_duration_t kCacheLead = ZX_SEC(15);
static constexpr zx_duration_t kCacheBacktrack = ZX_SEC(5);

static constexpr size_t kMaxBufferSize = 32 * 1024;

template <typename T>
zx_koid_t GetKoid(const fidl::InterfaceRequest<T>& request) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      request.channel().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

template <typename T>
zx_koid_t GetRelatedKoid(const fidl::InterfaceHandle<T>& request) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      request.channel().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.related_koid : ZX_KOID_INVALID;
}

}  // namespace

// static
std::unique_ptr<PlayerImpl> PlayerImpl::Create(
    fidl::InterfaceRequest<fuchsia::media::playback::Player> request,
    sys::ComponentContext* component_context, fit::closure quit_callback) {
  return std::make_unique<PlayerImpl>(std::move(request), component_context,
                                      std::move(quit_callback));
}

PlayerImpl::PlayerImpl(fidl::InterfaceRequest<fuchsia::media::playback::Player> request,
                       sys::ComponentContext* component_context, fit::closure quit_callback)
    : dispatcher_(async_get_default_dispatcher()),
      component_context_(component_context),
      quit_callback_(std::move(quit_callback)),
      core_(dispatcher_) {
  FX_DCHECK(request);
  FX_DCHECK(component_context_);
  FX_DCHECK(quit_callback_);

  ThreadPriority::SetToHigh();

  demux_factory_ = DemuxFactory::Create(this);
  FX_DCHECK(demux_factory_);
  decoder_factory_ = DecoderFactory::Create(this);
  FX_DCHECK(decoder_factory_);

  component_context_->outgoing()->debug_dir()->AddEntry(
      kDumpEntry,
      std::make_unique<vfs::PseudoFile>(
          kMaxBufferSize, [this](std::vector<uint8_t>* out, size_t max_bytes) -> zx_status_t {
            std::ostringstream os;

            os << fostr::NewLine << "duration:           " << AsNs(status_.duration);
            os << fostr::NewLine << "can pause:          " << status_.can_pause;
            os << fostr::NewLine << "can seek:           " << status_.can_seek;

            if (status_.metadata) {
              for (auto& property : status_.metadata->properties) {
                os << fostr::NewLine << property.label << ": " << property.value;
              }
            }

            os << fostr::NewLine << "state:              " << ToString(state_);
            if (state_ == State::kWaiting) {
              os << " " << waiting_reason_;
            }

            if (target_state_ != state_) {
              os << fostr::NewLine << "transitioning to:   " << ToString(target_state_);
            }

            if (target_position_ != Packet::kNoPts) {
              os << fostr::NewLine << "pending seek to:    " << AsNs(target_position_);
            }

            core_.Dump(os << std::boolalpha);
            os << "\n";

            auto out_str = os.str();
            auto end = out_str.size() > max_bytes ? out_str.begin() + max_bytes : out_str.end();
            *out = std::vector<uint8_t>(out_str.begin(), end);

            return ZX_OK;
          }));

  UpdateStatus();
  AddBindingInternal(std::move(request));

  bindings_.set_empty_set_handler([this]() { quit_callback_(); });

  core_.SetUpdateCallback([this]() {
    SendStatusUpdates();
    Update();
  });

  state_ = State::kInactive;
}

PlayerImpl::~PlayerImpl() {
  core_.SetUpdateCallback(nullptr);

  if (video_renderer_) {
    video_renderer_->SetGeometryUpdateCallback(nullptr);
  }
}

void PlayerImpl::MaybeCreateRenderer(StreamType::Medium medium) {
  if (core_.has_sink_segment(medium)) {
    // Renderer already exists.
    return;
  }

  switch (medium) {
    case StreamType::Medium::kAudio:
      if (!audio_renderer_) {
        auto audio = ServiceProvider::ConnectToService<fuchsia::media::Audio>();
        fuchsia::media::AudioRendererPtr audio_renderer;
        audio->CreateAudioRenderer(audio_renderer.NewRequest());
        audio_renderer_ = FidlAudioRenderer::Create(std::move(audio_renderer));
        core_.SetSinkSegment(RendererSinkSegment::Create(audio_renderer_, decoder_factory_.get()),
                             medium);
      }
      break;
    case StreamType::Medium::kVideo:
      if (!video_renderer_) {
        video_renderer_ = FidlVideoRenderer::Create(component_context_);
        video_renderer_->SetGeometryUpdateCallback([this]() { SendStatusUpdates(); });

        core_.SetSinkSegment(RendererSinkSegment::Create(video_renderer_, decoder_factory_.get()),
                             medium);
      }
      break;
    default:
      FX_DCHECK(false) << "Only audio and video are currently supported";
      break;
  }
}

void PlayerImpl::Update() {
  // This method is called whenever we might want to take action based on the
  // current state and recent events. The current state is in |state_|. Recent
  // events are recorded in |target_state_|, which indicates what state we'd
  // like to transition to, |target_position_|, which can indicate a position
  // we'd like to stream to, and |core_.end_of_stream()| which tells us we've
  // reached end of stream.
  //
  // The states are as follows:
  //
  // |kInactive|- Indicates that we have no source.
  // |kWaiting| - Indicates that we've done something asynchronous, and no
  //              further action should be taken by the state machine until that
  //              something completes (at which point the callback will change
  //              the state and call |Update|).
  // |kFlushed| - Indicates that presentation time is not progressing and that
  //              the pipeline is not primed with packets. This is the initial
  //              state and the state we transition to in preparation for
  //              seeking. A seek is currently only done when when the pipeline
  //              is clear of packets.
  // |kPrimed| -  Indicates that presentation time is not progressing and that
  //              the pipeline is primed with packets. We transition to this
  //              state when the client calls |Pause|, either from |kFlushed| or
  //              |kPlaying| state.
  // |kPlaying| - Indicates that presentation time is progressing and there are
  //              packets in the pipeline. We transition to this state when the
  //              client calls |Play|. If we're in |kFlushed| when |Play| is
  //              called, we transition through |kPrimed| state.
  //
  // The while loop that surrounds all the logic below is there because, after
  // taking some action and transitioning to a new state, we may want to check
  // to see if there's more to do in the new state. You'll also notice that
  // the callback lambdas generally call |Update|.
  while (true) {
    switch (state_) {
      case State::kInactive:
        if (setting_source_) {
          // Need to set the source. |FinishSetSource| will set the source and
          // post another call to |Update|.
          FinishSetSource();
        }
        return;

      case State::kFlushed:
        if (setting_source_) {
          // We have a new source. Get rid of the current source and transition
          // to inactive state. From there, we'll set up the new source.
          core_.ClearSourceSegment();

          // It's important to destroy the source at the same time we call
          // |ClearSourceSegment|, because the source has a raw pointer to the
          // source segment we just destroyed.
          current_source_ = nullptr;
          current_source_handle_ = nullptr;

          state_ = State::kInactive;
          break;
        }

        // Presentation time is not progressing, and the pipeline is clear of
        // packets.
        if (target_position_ != Packet::kNoPts) {
          // We want to seek. Enter |kWaiting| state until the operation is
          // complete.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to stop progressing prior to seek";

          // Capture the target position and clear it. If we get another
          // seek request while setting the timeline transform and and
          // seeking the source, we'll notice that and do those things
          // again.
          int64_t target_position = target_position_;
          target_position_ = Packet::kNoPts;

          // |program_range_min_pts_| will be delivered in the
          // |SetProgramRange| call, ensuring that the renderers discard
          // packets with PTS values less than the target position.
          // |transform_subject_time_| is used when setting the timeline.
          transform_subject_time_ = target_position;
          program_range_min_pts_ = target_position;

          SetTimelineFunction(0.0f, zx::clock::get_monotonic().get(), [this, target_position]() {
            if (target_position_ == target_position) {
              // We've had a rendundant seek request. Ignore
              // it.
              target_position_ = Packet::kNoPts;
            } else if (target_position_ != Packet::kNoPts) {
              // We've had a seek request to a new position.
              // Refrain from seeking the source and
              // re-enter this sequence.
              state_ = State::kFlushed;
              Update();
              return;
            }

            if (!core_.can_seek()) {
              // We can't seek, so |target_position| should
              // be zero.
              FX_DCHECK(target_position == 0)
                  << "Can't seek, target_position is " << target_position;
              state_ = State::kFlushed;
              Update();
            } else {
              // Seek to the new position.
              core_.Seek(target_position, [this]() {
                state_ = State::kFlushed;
                Update();
              });
            }
          });

          // Done for now. We're in kWaiting, and the callback will call
          // Update when the Seek call is complete.
          return;
        }

        if (target_state_ == State::kPlaying || target_state_ == State::kPrimed) {
          // We want to transition to |kPrimed| or to |kPlaying|, for which
          // |kPrimed| is a prerequisite. We enter |kWaiting| state, issue the
          // |SetProgramRange| and |Prime| requests and transition to |kPrimed|
          // when the operation is complete.
          state_ = State::kWaiting;
          waiting_reason_ = "for priming to complete";
          core_.SetProgramRange(0, program_range_min_pts_, Packet::kMaxPts);

          core_.Prime([this]() {
            state_ = State::kPrimed;
            ready_if_no_problem_ = true;
            Update();
          });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the prime is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPrimed:
        // Presentation time is not progressing, and the pipeline is primed with
        // packets.
        if (NeedToFlush()) {
          // Either we have a new source, want to seek, or we otherwise want to
          // flush.
          state_ = State::kWaiting;
          waiting_reason_ = "for flushing to complete";

          core_.Flush(ShouldHoldFrame(), [this]() {
            state_ = State::kFlushed;
            Update();
          });

          break;
        }

        if (target_state_ == State::kPlaying) {
          // We want to transition to |kPlaying|. Enter |kWaiting|, start the
          // presentation timeline and transition to |kPlaying| when the
          // operation completes.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to start progressing";
          SetTimelineFunction(1.0f, zx::clock::get_monotonic().get() + kMinimumLeadTime, [this]() {
            state_ = State::kPlaying;
            Update();
          });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the flush is complete.
          return;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kPlaying:
        // Presentation time is progressing, and packets are moving through
        // the pipeline.
        if (NeedToFlush() || target_state_ == State::kPrimed) {
          // Either we have a new source, we want to seek or we want to stop
          // playback. In any case, we need to enter |kWaiting|, stop the
          // presentation timeline and transition to |kPrimed| when the
          // operation completes.
          state_ = State::kWaiting;
          waiting_reason_ = "for renderers to stop progressing";
          SetTimelineFunction(0.0f, zx::clock::get_monotonic().get() + kMinimumLeadTime, [this]() {
            state_ = State::kPrimed;
            Update();
          });

          // Done for now. We're in |kWaiting|, and the callback will call
          // |Update| when the timeline is set.
          return;
        }

        if (core_.end_of_stream()) {
          // We've reached end of stream. The presentation timeline stops by
          // itself, so we just need to transition to |kPrimed|.
          target_state_ = State::kPrimed;
          state_ = State::kPrimed;
          // Loop around to check if there's more work to do.
          break;
        }

        // No interesting events to respond to. Done for now.
        return;

      case State::kWaiting:
        // Waiting for some async operation. Nothing to do until it completes.
        return;
    }
  }
}

void PlayerImpl::SetTimelineFunction(float rate, int64_t reference_time, fit::closure callback) {
  core_.SetTimelineFunction(
      media::TimelineFunction(transform_subject_time_, reference_time, media::TimelineRate(rate)),
      std::move(callback));
  transform_subject_time_ = Packet::kNoPts;
  SendStatusUpdates();
}

void PlayerImpl::SetFileSource(zx::channel file_channel) {
  BeginSetSource(CreateSource(FileReader::Create(std::move(file_channel)), nullptr));
}

void PlayerImpl::AddBindingInternal(
    fidl::InterfaceRequest<fuchsia::media::playback::Player> request) {
  FX_DCHECK(request);
  bindings_.AddBinding(this, std::move(request));

  // Fire |OnStatusChanged| event for the new client.
  bindings_.bindings().back()->events().OnStatusChanged(fidl::Clone(status_));
}

void PlayerImpl::BeginSetSource(std::unique_ptr<SourceImpl> source) {
  // Note the pending source change and advance the state machine. When the old
  // source (if any) is shut down, the state machine will call
  // |FinishSetSource|.
  new_source_ = std::move(source);

  setting_source_ = true;
  ready_if_no_problem_ = false;

  target_position_ = 0;
  async::PostTask(dispatcher_, [this]() { Update(); });
}

void PlayerImpl::FinishSetSource() {
  FX_DCHECK(setting_source_);
  FX_DCHECK(state_ == State::kInactive);
  FX_DCHECK(!core_.has_source_segment());

  setting_source_ = false;

  if (!new_source_) {
    // We were asked to clear the source which was already done by the state
    // machine. All we need to do is clean up the |SourceImpl| and handle
    // references.
    return;
  }

  state_ = State::kWaiting;
  waiting_reason_ = "for the source to initialize";
  program_range_min_pts_ = 0;
  transform_subject_time_ = 0;

  MaybeCreateRenderer(StreamType::Medium::kAudio);

  core_.SetSourceSegment(new_source_->TakeSourceSegment(), [this]() {
    state_ = State::kFlushed;
    SendStatusUpdates();
    Update();
  });

  current_source_ = std::move(new_source_);
  current_source_handle_ = std::move(new_source_handle_);
  FX_DCHECK(current_source_);
  // There's no handle if |SetFileSource| or |SetReaderSource| was used.
}

void PlayerImpl::Play() {
  target_state_ = State::kPlaying;
  Update();
}

void PlayerImpl::Pause() {
  if (target_state_ == State::kPlaying && !core_.can_pause()) {
    FX_LOGS(WARNING) << "Pause requested, cannot pause. Ignoring.";
    return;
  }

  target_state_ = State::kPrimed;
  Update();
}

void PlayerImpl::Seek(int64_t position) {
  if (!core_.can_seek()) {
    FX_LOGS(WARNING) << "Seek requested, cannot seek. Ignoring.";
    return;
  }

  target_position_ = position;
  Update();
}

void PlayerImpl::CreateView(fuchsia::ui::views::ViewToken view_token) {
  MaybeCreateRenderer(StreamType::Medium::kVideo);
  if (!video_renderer_) {
    return;
  }

  video_renderer_->CreateView(std::move(view_token));
}

void PlayerImpl::BindGainControl(
    fidl::InterfaceRequest<fuchsia::media::audio::GainControl> gain_control_request) {
  if (!audio_renderer_) {
    MaybeCreateRenderer(StreamType::Medium::kAudio);
  }

  FX_DCHECK(audio_renderer_);
  audio_renderer_->BindGainControl(std::move(gain_control_request));
}

void PlayerImpl::AddBinding(fidl::InterfaceRequest<fuchsia::media::playback::Player> request) {
  FX_DCHECK(request);
  AddBindingInternal(std::move(request));
}

void PlayerImpl::CreateFileSource(
    zx::channel file_channel,
    fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request) {
  FX_DCHECK(file_channel);
  FX_DCHECK(source_request);

  zx_koid_t koid = GetKoid(source_request);
  source_impls_by_koid_.emplace(
      koid, CreateSource(FileReader::Create(std::move(file_channel)), std::move(source_request),
                         [this, koid]() { source_impls_by_koid_.erase(koid); }));
}

void PlayerImpl::CreateReaderSource(
    fidl::InterfaceHandle<fuchsia::media::playback::SeekingReader> seeking_reader,
    fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request) {
  FX_DCHECK(seeking_reader);
  FX_DCHECK(source_request);

  zx_koid_t koid = GetKoid(source_request);
  source_impls_by_koid_.emplace(
      koid, CreateSource(FidlReader::Create(seeking_reader.Bind()), std::move(source_request),
                         [this, koid]() { source_impls_by_koid_.erase(koid); }));
}

void PlayerImpl::CreateElementarySource(
    int64_t duration_ns, bool can_pause, bool can_seek,
    std::unique_ptr<fuchsia::media::Metadata> metadata,
    fidl::InterfaceRequest<fuchsia::media::playback::ElementarySource> source_request) {
  FX_DCHECK(source_request);

  zx_koid_t koid = GetKoid(source_request);
  source_impls_by_koid_.emplace(
      koid, ElementarySourceImpl::Create(duration_ns, can_pause, can_seek, std::move(metadata),
                                         core_.graph(), std::move(source_request),
                                         [this, koid]() { source_impls_by_koid_.erase(koid); }));
}

void PlayerImpl::SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source> source_handle) {
  if (!source_handle) {
    BeginSetSource(nullptr);
    return;
  }

  // Keep |source_handle| in scope until we're done with the |SourceImpl|.
  // Otherwise, the |SourceImpl| will get a connection error and call its
  // remove callback.

  // The related koid for |source_handle| should be the same koid under which
  // we filed the |SourceImpl|.
  zx_koid_t source_koid = GetRelatedKoid(source_handle);

  auto iter = source_impls_by_koid_.find(source_koid);
  if (iter == source_impls_by_koid_.end()) {
    FX_LOGS(ERROR) << "Bad source handle passed to SetSource. Closing connection.";
    bindings_.CloseAll();
    return;
  }

  // Keep the handle around in case there are messages in the channel that need
  // to be processed.
  new_source_handle_ = std::move(source_handle);

  FX_DCHECK(iter->second);
  BeginSetSource(std::move(iter->second));
}

void PlayerImpl::TransitionToSource(fidl::InterfaceHandle<fuchsia::media::playback::Source> source,
                                    int64_t transition_pts, int64_t start_pts) {
  FX_NOTIMPLEMENTED();
  bindings_.CloseAll();
}

void PlayerImpl::CancelSourceTransition(
    fidl::InterfaceRequest<fuchsia::media::playback::Source> returned_source_request) {
  FX_NOTIMPLEMENTED();
  bindings_.CloseAll();
}

void PlayerImpl::ConnectToService(std::string service_path, zx::channel channel) {
  FX_DCHECK(component_context_);
  component_context_->svc()->Connect(service_path, std::move(channel));
}

std::unique_ptr<SourceImpl> PlayerImpl::CreateSource(
    std::shared_ptr<Reader> reader,
    fidl::InterfaceRequest<fuchsia::media::playback::Source> source_request,
    fit::closure connection_failure_callback) {
  std::shared_ptr<Demux> demux;
  demux_factory_->CreateDemux(ReaderCache::Create(reader), &demux);
  // TODO(dalesat): Handle CreateDemux failure.
  FX_DCHECK(demux);
  demux->SetCacheOptions(kCacheLead, kCacheBacktrack);
  return DemuxSourceImpl::Create(demux, core_.graph(), std::move(source_request),
                                 std::move(connection_failure_callback));
}

void PlayerImpl::SendStatusUpdates() {
  UpdateStatus();

  for (auto& binding : bindings_.bindings()) {
    binding->events().OnStatusChanged(fidl::Clone(status_));
  }
}

void PlayerImpl::UpdateStatus() {
  status_.timeline_function =
      fidl::MakeOptional(fidl::To<fuchsia::media::TimelineFunction>(core_.timeline_function()));
  status_.end_of_stream = core_.end_of_stream();
  status_.has_audio = core_.content_has_medium(StreamType::Medium::kAudio);
  status_.has_video = core_.content_has_medium(StreamType::Medium::kVideo);
  status_.audio_connected = core_.medium_connected(StreamType::Medium::kAudio);
  status_.video_connected = core_.medium_connected(StreamType::Medium::kVideo);

  status_.duration = core_.duration_ns();
  status_.can_pause = core_.can_pause();
  status_.can_seek = core_.can_seek();

  auto metadata = core_.metadata();
  status_.metadata =
      metadata ? fidl::MakeOptional(fidl::To<fuchsia::media::Metadata>(*metadata)) : nullptr;

  if (video_renderer_) {
    status_.video_size = CloneOptional(video_renderer_->video_size());
    status_.pixel_aspect_ratio = CloneOptional(video_renderer_->pixel_aspect_ratio());
  }

  status_.problem = CloneOptional(core_.problem());

  status_.ready = ready_if_no_problem_ && (status_.problem == nullptr);
}

// static
const char* PlayerImpl::ToString(State value) {
  switch (value) {
    case State::kInactive:
      return "inactive";
    case State::kWaiting:
      return "waiting";
    case State::kFlushed:
      return "flushed";
    case State::kPrimed:
      return "primed";
    case State::kPlaying:
      return "playing";
  }

  return "ILLEGAL STATE VALUE";
}

}  // namespace media_player
