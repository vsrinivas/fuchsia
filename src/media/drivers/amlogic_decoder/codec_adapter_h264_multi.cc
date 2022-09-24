// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_h264_multi.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/trace/event.h>
#include <lib/zx/bti.h>
#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/time.h>

#include <limits>
#include <mutex>
#include <optional>

#include <fbl/algorithm.h>
#include <src/media/lib/memory_barriers/memory_barriers.h>

#include "device_ctx.h"
#include "h264_multi_decoder.h"
#include "macros.h"
#include "pts_manager.h"
#include "vdec1.h"

namespace amlogic_decoder {

namespace {
static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

template <typename T>
constexpr T AlignUpConstexpr(T value, T divisor) {
  return (value + divisor - 1) / divisor * divisor;
}

// For experimentation purposes, this allows easily switching (with a local edit) to allowing larger
// input buffers, and using a larger stream buffer.  When this is false, the input buffer max size
// and stream buffer size are tuned for decoding 1080p safely (barely).
constexpr bool k4kInputFrames = false;

// See VLD_PADDING_SIZE.
constexpr uint32_t kPaddingSize = 1024;

// This should be enough space to hold all headers (such as SEI, SPS, PPS), plus the kPaddingSize
// padding the decoder adds after each header that is delivered in its own packet, before a max-size
// frame.  This should be large enough to also hold any zero-padding in the input stream before a
// max-size frame (typically none).
constexpr uint32_t kBigHeadersBytes = 128 * 1024;

// This is enough to decode 4:2:0 1920x1080 with MinCR 2, assuming headers before the frame don't
// exceed 128KiB.
constexpr uint32_t k1080pMaxCompressedFrameSize = 1920 * 1080 * 3 / 2 / 2;
constexpr uint32_t kDci4kMaxCompressedFrameSize = 4096u * 2160 * 3 / 2 / 2;

constexpr uint32_t k1080pMaxCompressedFrameSizeIncludingHeaders =
    k1080pMaxCompressedFrameSize + kBigHeadersBytes;
constexpr uint32_t kDci4kMaxCompressedFrameSizeIncludingHeaders =
    kDci4kMaxCompressedFrameSize + kBigHeadersBytes;

constexpr uint32_t kMaxCompressedFrameSizeIncludingHeaders =
    k4kInputFrames ? kDci4kMaxCompressedFrameSizeIncludingHeaders
                   : k1080pMaxCompressedFrameSizeIncludingHeaders;

constexpr uint32_t kStreamBufferReadAlignment = 512;
// It might be reasonable to remove this adjustment, given some experimentation to see if the
// kStreamBufferReadAlignment is sufficient on its own to make kStreamBufferSize work.
constexpr uint32_t kReadNotEqualWriteAdjustment = 1;

// The ZX_PAGE_SIZE alignment is just because we won't really allocate a partial page via sysmem
// anyway, so we may as well use the rest of the last needed page even if kStreamBufferReadAlignment
// might technically work.
//
// The first kPaddingSize is to be able to flush through a first frame.  The second kPaddingSize is
// because the first kPaddingSize is still in the stream buffer at the time we're decoding the
// second frame, because the first frame ended just after the first frame's payload data as far as
// the HW is concerned (despite the need for padding to cause the first frame to complete).
constexpr uint32_t kStreamBufferSize =
    AlignUpConstexpr(kMaxCompressedFrameSizeIncludingHeaders + 2 * kPaddingSize +
                         kStreamBufferReadAlignment + kReadNotEqualWriteAdjustment,
                     static_cast<uint32_t>(ZX_PAGE_SIZE));
static_assert(kStreamBufferSize % ZX_PAGE_SIZE == 0);

// For now we rely on a compressed input frame to be contained entirely in a single buffer.  While
// this minimum size may work for some demo streams, for now clients are expected to set a larger
// min_buffer_size for input, in their BufferCollectionConstraints.  A recommended expression for
// min_buffer_size is max_width * max_height * 3 / 2 / 2 + 128 * 1024.  This recommended expression
// accounts for MinCR (see h264 spec) of 2 which is worst-case, and allows for SEI/SPS/PPS that's up
// to 128 KiB which is probably enough for those headers.
constexpr uint32_t kInputPerPacketBufferBytesMin = 512 * 1024;

const uint32_t kInputPerPacketBufferBytesMax = kMaxCompressedFrameSizeIncludingHeaders;

constexpr uint32_t kInputBufferCountForCodecMin = 1;
constexpr uint32_t kInputBufferCountForCodecMax = 64;

}  // namespace

CodecAdapterH264Multi::CodecAdapterH264Multi(std::mutex& lock,
                                             CodecAdapterEvents* codec_adapter_events,
                                             DeviceCtx* device)
    : AmlogicCodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()),
      core_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      shared_fidl_thread_closure_queue_(std::in_place,
                                        device->driver()->shared_fidl_loop()->dispatcher(),
                                        device->driver()->shared_fidl_thread()) {
  ZX_DEBUG_ASSERT(device_);
  ZX_DEBUG_ASSERT(video_);
  ZX_DEBUG_ASSERT(secure_memory_mode_[kInputPort] == fuchsia::mediacodec::SecureMemoryMode::OFF);
  ZX_DEBUG_ASSERT(secure_memory_mode_[kOutputPort] == fuchsia::mediacodec::SecureMemoryMode::OFF);
  thrd_t thrd;
  zx_status_t status;
  status = core_loop_.StartThread("H264 Core loop", &thrd);
  ZX_ASSERT(status == ZX_OK);

  device_->SetThreadProfile(zx::unowned_thread(thrd_get_zx_handle(thrd)),
                            ThreadRole::kH264MultiCore);

  status = resource_loop_.StartThread("Resource loop");
  ZX_ASSERT(status == ZX_OK);
}

CodecAdapterH264Multi::~CodecAdapterH264Multi() {
  // We need to delete the shared_fidl_thread_closure_queue_ on its dispatcher thread, per the
  // rules of ~ClosureQueue.
  sync_completion_t shared_fidl_finished;
  auto run_on_shared_fidl = [this, &shared_fidl_finished] {
    shared_fidl_thread_closure_queue_.reset();
    sync_completion_signal(&shared_fidl_finished);
  };
  if (thrd_current() == device_->driver()->shared_fidl_thread()) {
    run_on_shared_fidl();
  } else {
    shared_fidl_thread_closure_queue_->Enqueue(run_on_shared_fidl);
  }
  sync_completion_wait(&shared_fidl_finished, ZX_TIME_INFINITE);

  core_loop_.Shutdown();
  resource_loop_.Shutdown();
}

void CodecAdapterH264Multi::SetCodecDiagnostics(CodecDiagnostics* codec_diagnostics) {
  codec_diagnostics_ = codec_diagnostics->CreateDriverCodec("H264");
}

std::optional<media_metrics::StreamProcessorEvents2MigratedMetricDimensionImplementation>
CodecAdapterH264Multi::CoreCodecMetricsImplementation() {
  return media_metrics::
      StreamProcessorEvents2MigratedMetricDimensionImplementation_AmlogicDecoderH264;
}

bool CodecAdapterH264Multi::IsCoreCodecRequiringOutputConfigForFormatDetection() { return false; }

bool CodecAdapterH264Multi::IsCoreCodecMappedBufferUseful(CodecPort port) {
  if (port == kInputPort) {
    // Returning true here essentially means that we may be able to make use of mapped buffers if
    // they're possible.  However if is_secure true, we won't get a mapping and we don't really need
    // a mapping, other than for avcC.  If avcC shows up on input, we'll fail then.
    //
    // TODO(fxbug.dev/35200): Add the failure when avcC shows up when is_secure, as described above.
    return true;
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    return false;
  }
}

bool CodecAdapterH264Multi::IsCoreCodecHwBased(CodecPort port) { return true; }

zx::unowned_bti CodecAdapterH264Multi::CoreCodecBti() { return zx::unowned_bti(video_->bti()); }

void CodecAdapterH264Multi::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  initial_input_format_details_ = fidl::Clone(initial_input_format_details);
  latest_input_format_details_ = fidl::Clone(initial_input_format_details);

  // TODO(dustingreen): We do most of the setup in CoreCodecStartStream()
  // currently, but we should do more here and less there.
}

void CodecAdapterH264Multi::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {
  // TODO(fxbug.dev/40198): Ideally a codec list from the main CodecFactory would avoid reporting
  // support for secure output or input when !is_tee_available(), which likely will mean reporting
  // that in list from driver's local codec factory up to main factory.  The main CodecFactory could
  // also avoid handing out a codec that can't do secure output / input when the TEE isn't
  // available, so we wouldn't end up here.
  if (secure_memory_mode != fuchsia::mediacodec::SecureMemoryMode::OFF &&
      !video_->is_tee_available()) {
    events_->onCoreCodecFailCodec(
        "BUG 40198 - Codec factory should catch earlier when secure requested without TEE.");
    return;
  }
  secure_memory_mode_[port] = secure_memory_mode;
}

void CodecAdapterH264Multi::OnFrameReady(std::shared_ptr<VideoFrame> frame) {
  TRACE_DURATION("media", "CodecAdapterH264Multi::OnFrameReady", "index", frame->index);
  output_stride_ = frame->stride;
  const CodecBuffer* buffer = frame->codec_buffer;
  ZX_DEBUG_ASSERT(buffer);

  uint64_t total_size_bytes = frame->stride * frame->coded_height * 3 / 2;
  if (total_size_bytes > std::numeric_limits<uint32_t>::max()) {
    OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
    return;
  }

  // The Codec interface requires that emitted frames are cache clean. We invalidate without
  // skipping over stride-width per line, at least partly because stride - width is small (possibly
  // always 0) for this decoder. But we do invalidate the UV section separately in case
  // uv_plane_offset happens to leave significant space after the Y section (regardless of whether
  // there's actually ever much padding there).
  //
  // TODO(dustingreen): Probably there's not ever any significant
  // padding between Y and UV for this decoder, so probably can make one
  // invalidate call here instead of two with no downsides.
  // TODO(jbauman): avoid unnecessary cache ops when in RAM domain or when the buffer isn't
  // mappable.
  {
    TRACE_DURATION("media", "cache invalidate");
    if (!IsOutputSecure()) {
      buffer->CacheFlushAndInvalidate(0, frame->stride * frame->coded_height);
      buffer->CacheFlushAndInvalidate(frame->uv_plane_offset,
                                      frame->stride * frame->coded_height / 2);
    }
  }

  // We intentionally _don't_ use the packet with same index as the buffer (in
  // general - it's fine that they sometimes match), to avoid clients building
  // up inappropriate dependency on buffer index being the same as packet
  // index (as nice as that would be, VP9, and maybe others, don't get along
  // with that in general, so ... force clients to treat packet index and
  // buffer index as separate things).
  //
  // Associate buffer with packet while the packet is in-flight.
  CodecPacket* packet = GetFreePacket(buffer);
  // With h.264, we know that an emitted buffer implies an available output
  // packet, because h.264 doesn't put the same output buffer in flight more
  // than once concurrently, and we have as many output packets as buffers.
  // This contrasts with VP9 which has unbounded show_existing_frame.
  ZX_DEBUG_ASSERT(packet);

  packet->SetStartOffset(0);
  packet->SetValidLengthBytes(static_cast<uint32_t>(total_size_bytes));

  if (frame->has_pts) {
    packet->SetTimstampIsh(frame->pts);
  } else {
    packet->ClearTimestampIsh();
  }

  events_->onCoreCodecOutputPacket(packet, false, false);
}

void CodecAdapterH264Multi::OnError() {
  LOG(ERROR, "OnError()");
  OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterH264Multi::CoreCodecStartStream() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_input_format_details_pending_ = true;
    // At least until proven otherwise.
    is_avcc_ = false;
    is_input_end_of_stream_queued_ = false;
    is_stream_failed_ = false;
  }  // ~lock

  // Encapsulate stream buffer allocation in closure so that it can be posted on the resource thread
  auto resource_init_function = fit::closure([this] {  // scope lock
    TRACE_DURATION("media", "Decoder Initialization");
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    // The output port is the one we really care about for is_secure of the decoder, since the HW
    // can read from secure or non-secure even when in secure mode, but can only write to secure
    // memory when in secure mode.
    //
    // Must create under lock to ensure that a potential other instance that incremented power
    // ref(s) first is fully done un-gating clocks.
    auto decoder = std::make_unique<H264MultiDecoder>(video_, this, this, IsOutputSecure());
    if (codec_diagnostics_) {
      decoder->SetCodecDiagnostics(&codec_diagnostics_.value());
    }
    if (decoder->InitializeBuffers() != ZX_OK) {
      events_->onCoreCodecFailCodec("InitializeBuffers() failed");
      return;
    }
    decoder_ = decoder.get();
    auto decoder_instance =
        std::make_unique<DecoderInstance>(std::move(decoder), video_->vdec1_core());
    StreamBuffer* buffer = decoder_instance->stream_buffer();
    video_->AddNewDecoderInstance(std::move(decoder_instance));
    if (video_->AllocateStreamBuffer(buffer, kStreamBufferSize, /*use_parser=*/true,
                                     IsOutputSecure()) != ZX_OK) {
      // Log here instead of in AllocateStreamBuffer() since video_ doesn't know which codec this is
      // for.
      events_->onCoreCodecLogEvent(
          media_metrics::StreamProcessorEvents2MigratedMetricDimensionEvent::AllocationError);
      events_->onCoreCodecFailCodec("AllocateStreamBuffer() failed");
      return;
    }
    // ~lock
  });
  PostAndBlockResourceTask(std::move(resource_init_function));
}

void CodecAdapterH264Multi::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the FormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterH264Multi::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterH264Multi::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the HW to necessarily decode all
  // the way up to the marker, depending on whether the client closes the stream
  // or switches to a different stream first - in those cases it's fine for the
  // marker to never show up as output EndOfStream.

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_input_end_of_stream_queued_ = true;
  }  // ~lock

  QueueInputItem(CodecInputItem::EndOfStream());
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
void CodecAdapterH264Multi::CoreCodecStopStream() {
  std::list<CodecInputItem> leftover_input_items = CoreCodecStopStreamInternal();
  for (auto& input_item : leftover_input_items) {
    if (input_item.is_packet()) {
      events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
    }
  }
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
std::list<CodecInputItem> CodecAdapterH264Multi::CoreCodecStopStreamInternal() {
  std::list<CodecInputItem> input_items_result;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    bool is_cancelling_input_processing = true;
    std::condition_variable stop_input_processing_condition;
    async::PostTask(core_loop_.dispatcher(),
                    [this, &stop_input_processing_condition, &input_items_result,
                     &is_cancelling_input_processing] {
                      {  // scope lock
                        std::lock_guard<std::mutex> lock(lock_);
                        ZX_DEBUG_ASSERT(input_items_result.empty());
                        input_items_result.swap(input_queue_);
                        is_cancelling_input_processing = false;
                      }  // ~lock
                      stop_input_processing_condition.notify_all();
                    });
    while (is_cancelling_input_processing) {
      stop_input_processing_condition.wait(lock);
    }
    ZX_DEBUG_ASSERT(!is_cancelling_input_processing);
  }  // ~lock
  LOG(DEBUG, "RemoveDecoder()...");

  auto resource_destroy_function = fit::closure([this] {
    TRACE_DURATION("media", "Decoder Destruction");
    std::lock_guard<std::mutex> decoder_lock(*video_->video_decoder_lock());
    if (decoder_) {
      video_->RemoveDecoderLocked(decoder_);
      decoder_ = nullptr;
    }
  });
  PostAndBlockResourceTask(std::move(resource_destroy_function));

  LOG(DEBUG, "RemoveDecoder() done.");
  return input_items_result;
}

void CodecAdapterH264Multi::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
  ZX_DEBUG_ASSERT(port == kOutputPort);

  // This flush is to eliminate any dirty cache lines.  Our only choices are flush or flush and
  // invalidate, so it's maybe slightly cheaper to only flush.  We don't care what's being flushed
  // here, if anything, since the buffer will be overwritten by HW decoding into the buffer anyway.
  //
  // There's a flush+invalidate later after the HW is done decoding, which we do for the invalidate
  // part.  For that flush to not overwrite anything the HW wrote to the buffer, this flush
  // eliminates any dirty cache lines that might otherwise get flushed after HW has written to the
  // buffer.
  if (!IsOutputSecure()) {
    ZX_DEBUG_ASSERT(buffer->size() <= std::numeric_limits<uint32_t>::max());
    buffer->CacheFlush(0, static_cast<uint32_t>(buffer->size()));
  }

  all_output_buffers_.push_back(buffer);
}

void CodecAdapterH264Multi::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port != kOutputPort) {
    return;
  }
  ZX_DEBUG_ASSERT(port == kOutputPort);
  // output

  ZX_DEBUG_ASSERT(all_output_packets_.empty());
  ZX_DEBUG_ASSERT(free_output_packets_.empty());
  ZX_DEBUG_ASSERT(!all_output_buffers_.empty());
  ZX_DEBUG_ASSERT(all_output_buffers_.size() <= packets.size());
  for (auto& packet : packets) {
    all_output_packets_.push_back(packet.get());
    free_output_packets_.push_back(packet.get()->packet_index());
  }
  // This should prevent any inadvertent dependence by clients on the ordering
  // of packet_index values in the output stream or any assumptions re. the
  // relationship between packet_index and buffer_index.
  std::shuffle(free_output_packets_.begin(), free_output_packets_.end(), not_for_security_prng_);
}

void CodecAdapterH264Multi::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  ZX_DEBUG_ASSERT(!packet->is_new());

  // A recycled packet will have a buffer set because the packet is in-flight
  // until put on the free list, and has a buffer associated while in-flight.
  const CodecBuffer* buffer = packet->buffer();
  ZX_DEBUG_ASSERT(buffer);

  // Eliminate any dirty CPU cache lines, so that later when we do a flush+invalidate after HW is
  // done writing to the buffer, we won't be writing over anything the HW wrote.
  if (!IsOutputSecure()) {
    ZX_DEBUG_ASSERT(buffer->size() <= std::numeric_limits<uint32_t>::max());
    buffer->CacheFlush(0, static_cast<uint32_t>(buffer->size()));
  }

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // Getting the buffer is all we needed the packet for.  The packet won't get re-used until it
    // goes back on the free list below.
    //
    // This must be done under lock_ to synchronize with InitializeFrames() with
    // IsCurrentOutputBufferCollectionUsable() true.  In particular we need to this synchronized to
    // be able to tell InitializedFrames() exactly which CodecFrame(s) are initially free vs.
    // initially used, based on which CodecFrame(s) correspond to a packet that is not free and has
    // a specific buffer which corresponds to a not-free CodecFrame.
    packet->SetBuffer(nullptr);
    free_output_packets_.push_back(packet->packet_index());
  }  // ~lock

  async::PostTask(core_loop_.dispatcher(), [this, video_frame = buffer->video_frame()]() {
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    std::shared_ptr<VideoFrame> frame = video_frame.lock();
    if (!frame) {
      // EndOfStream seen at the output, or a new InitializeFrames(), can cause
      // !frame, which is fine.  In that case, any new stream will request
      // allocation of new frames.
      return;
    }
    if (!decoder_)
      return;
    // Potentially this also pumps the decoder under video_decoder_lock.
    decoder_->ReturnFrame(std::move(frame));
  });
}

void CodecAdapterH264Multi::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  DLOG("port: %u", port);
  std::lock_guard<std::mutex> lock(lock_);

  // This adapter should ensure that zero old CodecPacket* or CodecBuffer*
  // remain in this adapter (or below).

  if (port == kInputPort) {
    // There shouldn't be any queued input at this point, but if there is any,
    // fail here even in a release build.
    ZX_ASSERT(input_queue_.empty());
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);

    // The old all_output_buffers_ are no longer valid.
    all_output_buffers_.clear();
    all_output_packets_.clear();
    free_output_packets_.clear();
    output_buffer_collection_info_ = std::nullopt;
  }
  buffer_settings_[port] = std::nullopt;
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterH264Multi::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  // This decoder produces NV12.

  auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();

  // For the moment, we always require buffer reallocation for any output constraints change.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // Ensure that if the client allocates its max + the server max that it won't go over the hardware
  // limit (max_buffer_count).
  if (max_buffer_count_[kOutputPort] <= min_buffer_count_[kOutputPort]) {
    events_->onCoreCodecFailCodec("Impossible for client to satisfy buffer counts");
    return nullptr;
  }

  return config;
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterH264Multi::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  fuchsia::sysmem::BufferCollectionConstraints result;

  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  if (port == kInputPort) {
    // We don't override CoreCodecBuildNewInputConstraints() for now, so pick these up from what was
    // set by default implementation of CoreCodecBuildNewInputConstraints().
    min_buffer_count_[kInputPort] = kInputBufferCountForCodecMin;
    max_buffer_count_[kInputPort] = kInputBufferCountForCodecMax;
  }

  ZX_DEBUG_ASSERT(min_buffer_count_[port] != 0);
  ZX_DEBUG_ASSERT(max_buffer_count_[port] != 0);

  result.min_buffer_count_for_camping = min_buffer_count_[port];

  // Some slack is nice overall, but avoid having each participant ask for
  // dedicated slack.  Using sysmem the client will ask for it's own buffers for
  // camping and any slack, so the codec doesn't need to ask for any extra on
  // behalf of the client.
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  result.max_buffer_count = max_buffer_count_[port];

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kInputPort) {
    per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax;
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    // NV12, based on min stride.
    per_packet_buffer_bytes_min = min_stride_ * height_ * 3 / 2;
    // At least for now, don't cap the per-packet buffer size for output.  The
    // HW only cares about the portion we set up for output anyway, and the
    // client has no way to force output to occur into portions of the output
    // buffer beyond what's implied by the max supported image dimensions.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;
  // amlogic requires physically contiguous on both input and output
  result.buffer_memory_constraints.physically_contiguous_required = true;
  result.buffer_memory_constraints.secure_required = IsPortSecureRequired(port);
  result.buffer_memory_constraints.cpu_domain_supported = !IsPortSecureRequired(port);
  result.buffer_memory_constraints.ram_domain_supported =
      !IsPortSecureRequired(port) && (port == kOutputPort);

  if (IsPortSecurePermitted(port)) {
    result.buffer_memory_constraints.inaccessible_domain_supported = true;
    fuchsia::sysmem::HeapType secure_heap = (port == kInputPort)
                                                ? fuchsia::sysmem::HeapType::AMLOGIC_SECURE_VDEC
                                                : fuchsia::sysmem::HeapType::AMLOGIC_SECURE;
    result.buffer_memory_constraints
        .heap_permitted[result.buffer_memory_constraints.heap_permitted_count++] = secure_heap;
  }

  if (!IsPortSecureRequired(port)) {
    result.buffer_memory_constraints
        .heap_permitted[result.buffer_memory_constraints.heap_permitted_count++] =
        fuchsia::sysmem::HeapType::SYSTEM_RAM;
  }

  if (port == kOutputPort) {
    result.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints = result.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
    // TODO(fxbug.dev/13532): confirm that REC709 is always what we want here, or plumb
    // actual YUV color space if it can ever be REC601_*.  Since 2020 and 2100
    // are minimum 10 bits per Y sample and we're outputting NV12, 601 is the
    // only other potential possibility here.
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;

    // The non-"required_" fields indicate the decoder's ability to potentially
    // output frames at various dimensions as coded in the stream.  Aside from
    // the current stream being somewhere in these bounds, these have nothing to
    // do with the current stream in particular.
    image_constraints.min_coded_width = 16;
    image_constraints.max_coded_width = 4096;
    image_constraints.min_coded_height = 16;
    // This intentionally isn't the _height_ of a 4096x2176 frame, it's
    // intentionally the _width_ of a 4096x2176 frame assigned to
    // max_coded_height.
    //
    // See max_coded_width_times_coded_height.  We intentionally constrain the
    // max dimension in width or height to the width of a 4096x2176 frame.
    // While the HW might be able to go bigger than that as long as the other
    // dimension is smaller to compensate, we don't really need to enable any
    // larger than 4096x2176's width in either dimension, so we don't.
    image_constraints.max_coded_height = 4096;
    image_constraints.min_bytes_per_row = 16;
    // no hard-coded max stride, at least for now
    image_constraints.max_bytes_per_row = 0xFFFFFFFF;
    image_constraints.max_coded_width_times_coded_height = 4096 * 2176;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 16;
    image_constraints.coded_height_divisor = 16;
    image_constraints.bytes_per_row_divisor = H264MultiDecoder::kStrideAlignment;
    // Even though we only ever output at offset 0, sysmem defaults start_offset_divisor to the
    // image format alignment which is 2 for NV12. Since we are a producer, we should fully specify
    // here so late attach clients don't have to specify it explicitly.
    image_constraints.start_offset_divisor = 2;
    // Odd display dimensions are permitted, but these don't imply odd NV12
    // dimensions - those are constrainted by coded_width_divisor and
    // coded_height_divisor which are both 16.
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    // The decoder is producing frames and the decoder has no choice but to
    // produce frames at their coded size.  The decoder wants to potentially be
    // able to support a stream with dynamic resolution, potentially including
    // dimensions both less than and greater than the dimensions that led to the
    // current need to allocate a BufferCollection.  For this reason, the
    // required_ fields are set to the exact current dimensions, and the
    // permitted (non-required_) fields is set to the full potential range that
    // the decoder could potentially output.  If an initiator wants to require a
    // larger range of dimensions that includes the required range indicated
    // here (via a-priori knowledge of the potential stream dimensions), an
    // initiator is free to do so.
    image_constraints.required_min_coded_width = width_;
    image_constraints.required_max_coded_width = width_;
    image_constraints.required_min_coded_height = height_;
    image_constraints.required_max_coded_height = height_;
  } else {
    ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);
  }

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

void CodecAdapterH264Multi::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  if (port == kOutputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.has_image_format_constraints);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
                    fuchsia::sysmem::PixelFormatType::NV12);
    output_buffer_collection_info_ = fidl::Clone(buffer_collection_info);
  }
  buffer_settings_[port].emplace(buffer_collection_info.settings);
  ZX_DEBUG_ASSERT(IsPortSecure(port) || !IsPortSecureRequired(port));
  ZX_DEBUG_ASSERT(!IsPortSecure(port) || IsPortSecurePermitted(port));
  // TODO(dustingreen): Remove after secure video decode works e2e.
  LOG(DEBUG,
      "CodecAdapterH264Multi::CoreCodecSetBufferCollectionInfo() - IsPortSecure(): %u port: %u",
      IsPortSecure(port), port);
}

fuchsia::media::StreamOutputFormat CodecAdapterH264Multi::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()->set_format_details_version_ordinal(
      new_output_format_details_version_ordinal);

  result.mutable_format_details()->set_mime_type("video/raw");

  // For the moment, we'll memcpy to NV12 without any extra padding.
  fuchsia::media::VideoUncompressedFormat video_uncompressed;
  video_uncompressed.fourcc = make_fourcc('N', 'V', '1', '2');
  video_uncompressed.primary_width_pixels = width_;
  video_uncompressed.primary_height_pixels = height_;
  video_uncompressed.secondary_width_pixels = width_ / 2;
  video_uncompressed.secondary_height_pixels = height_ / 2;
  // TODO(dustingreen): remove this field from the VideoUncompressedFormat or
  // specify separately for primary / secondary.
  video_uncompressed.planar = true;
  video_uncompressed.swizzled = false;
  video_uncompressed.primary_line_stride_bytes = output_stride_;
  video_uncompressed.secondary_line_stride_bytes = output_stride_;
  video_uncompressed.primary_start_offset = 0;
  video_uncompressed.secondary_start_offset = output_stride_ * height_;
  video_uncompressed.tertiary_start_offset = output_stride_ * height_ + 1;
  video_uncompressed.primary_pixel_stride = 1;
  video_uncompressed.secondary_pixel_stride = 2;
  video_uncompressed.primary_display_width_pixels = display_width_;
  video_uncompressed.primary_display_height_pixels = display_height_;
  video_uncompressed.has_pixel_aspect_ratio = has_sar_;
  video_uncompressed.pixel_aspect_ratio_width = sar_width_;
  video_uncompressed.pixel_aspect_ratio_height = sar_height_;

  // TODO(dustingreen): Deprecate and remove fields set above.  Use only these fields (or newer
  // variant of these fields; TBD):
  video_uncompressed.image_format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  video_uncompressed.image_format.coded_width = width_;
  video_uncompressed.image_format.coded_height = height_;
  video_uncompressed.image_format.bytes_per_row = output_stride_;
  video_uncompressed.image_format.display_width = display_width_;
  video_uncompressed.image_format.display_height = display_height_;
  video_uncompressed.image_format.layers = 1;
  video_uncompressed.image_format.color_space.type = fuchsia::sysmem::ColorSpaceType::REC709;
  video_uncompressed.image_format.has_pixel_aspect_ratio = has_sar_;
  video_uncompressed.image_format.pixel_aspect_ratio_width = sar_width_;
  video_uncompressed.image_format.pixel_aspect_ratio_height = sar_height_;

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(std::move(video_uncompressed));

  result.mutable_format_details()->mutable_domain()->set_video(std::move(video_format));

  return result;
}

void CodecAdapterH264Multi::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, the core codec just needs us to get new frame buffers
  // set up, so nothing to do here.
  //
  // CoreCodecEnsureBuffersNotConfigured() will run soon.
}

void CodecAdapterH264Multi::CoreCodecMidStreamOutputBufferReConfigFinish() {
  MidStreamOutputBufferConfigInternal(true);
}

void CodecAdapterH264Multi::MidStreamOutputBufferConfigInternal(bool did_reallocate_buffers) {
  // Now that the client has configured output buffers, we need to hand those
  // back to the core codec via InitializedFrames.

  std::vector<CodecFrame> frames;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // Now we need to populate the frames_out vector.
    for (uint32_t i = 0; i < all_output_buffers_.size(); i++) {
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->index() == i);
      frames.emplace_back(*all_output_buffers_[i]);
      frames.back().initial_usage_count() = 0;
    }
    for (auto* codec_packet : all_output_packets_) {
      // The buffer() being non-null corresponds to the packet index not being in
      // free_output_packets_.  In other words, the non-null buffer() fields among all packets is
      // all the used buffers.  The buffer indexes and frames indexes are the same due to how frames
      // is populated above.
      if (codec_packet->buffer()) {
        // This won't happen if we're doing a CoreCodecMidStreamOutputBufferReConfigFinish().  In
        // that case we cleared all the packets and buffers and allocated new ones, so there won't
        // be any packet with an assigned buffer, since there aren't any packets from before.
        //
        // On the other hand if we're telling an H264MultiDecoder about buffers that aren't new and
        // may still be in flight on output, we some are not initially free ("initially" as in when
        // InitializedFrames() is called).
        //
        // When !did_reallocate_buffers, we know that CoreCodecRecycleOutputPacket() won't be
        // running for the entire MidStreamOutputBufferConfigInternal().  It's really that fact
        // rather than the present lock_ interval that makes this initial_usage_count() stuff
        // synchronize properly with CoreCodecRecycleOutputPacket().
        ZX_DEBUG_ASSERT(!did_reallocate_buffers);
        // h.264 doesn't have anything like vp9's show_existing_frame, so a given buffer is only
        // downstream up to once at a time, so we know we won't see any CodecFrame/CodecBuffer
        // that's currently referenced by more than one packet.
        ZX_DEBUG_ASSERT(frames[codec_packet->buffer()->index()].initial_usage_count() == 0);
        frames[codec_packet->buffer()->index()].initial_usage_count() = 1;
      }
    }
    width = width_;
    height = height_;
    stride = fbl::round_up(
        width,
        output_buffer_collection_info_->settings.image_format_constraints.bytes_per_row_divisor);
  }  // ~lock

  auto resource_init_function =
      fit::closure([this, width, height, stride, frames = std::move(frames)]() mutable {
        TRACE_DURATION("media", "Decoder Frame Initialization");
        std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
        decoder_->InitializedFrames(std::move(frames), width, height, stride);
      });
  // When we're really doing a mid-stream change, or when two consecutive streams are effectively
  // part of an overall logical stream from the user's point of view (such as an upper-layer
  // mid-video dimension change that ends up switching streams at this layer), posting over to a
  // "resource" thread with different scheduler profile isn't really all that rigorous, since the
  // resource setup aspects are inherently part of what needs to get done to achieve consistent
  // output timing of decoded frames.  But by doing this we can avoid needing to boost the scheduler
  // profile budget for the current thread, at least for now (which again, isn't particularly
  // rigorous, but it's why this is posting and immediately waiting).  It's also relevant that the
  // scheduler presently has an "anti-abuse" behavior where a thread gets de-scheduled each time it
  // enables a deadline profile, so that's why we post-and-wait here instead of having the current
  // thread switch its own scheduler deadline profile off/on.  This is entirely about optimizing
  // stream startup duration in the common case (which of course matters), not about rigor for
  // scheduling aspects of mid-stream dimension change (which is fine for now, but may be improved).
  PostAndBlockResourceTask(std::move(resource_init_function));

  async::PostTask(core_loop_.dispatcher(), [this] {
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    if (!decoder_) {
      return;
    }
    // Something else may have come along since InitializedFrames and pumped the decoder, but that's
    // ok.
    decoder_->PumpOrReschedule();
  });
}

void CodecAdapterH264Multi::PostAndBlockResourceTask(fit::closure task_function) {
  sync_completion_t resource_finished;
  auto task =
      fit::closure([&resource_finished, task_function = std::move(task_function)]() mutable {
        task_function();
        sync_completion_signal(&resource_finished);
      });

  zx_status_t task_result = async::PostTask(resource_loop_.dispatcher(), std::move(task));
  if (task_result != ZX_OK) {
    LOG(ERROR, "Could not post task to resource thread");
  }

  sync_completion_wait(&resource_finished, ZX_TIME_INFINITE);
}

void CodecAdapterH264Multi::QueueInputItem(CodecInputItem input_item, bool at_front) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // For now we don't worry about avoiding a trigger if we happen to queue
    // when ProcessInput() has removed the last item but ProcessInput() is still
    // running.
    if (at_front) {
      input_queue_.emplace_front(std::move(input_item));
    } else {
      input_queue_.emplace_back(std::move(input_item));
    }
    if (!have_queued_trigger_decoder_) {
      have_queued_trigger_decoder_ = true;
      async::PostTask(core_loop_.dispatcher(), [this]() {
        {
          std::lock_guard<std::mutex> lock(lock_);
          have_queued_trigger_decoder_ = false;
        }
        std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
        if (!decoder_)
          return;
        decoder_->ReceivedNewInput();
      });
    }
  }  // ~lock
}

CodecInputItem CodecAdapterH264Multi::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

std::optional<H264MultiDecoder::DataInput> CodecAdapterH264Multi::ReadMoreInputData() {
  H264MultiDecoder::DataInput result;
  while (true) {
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      return std::nullopt;
    }

    if (item.is_format_details()) {
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      ZX_ASSERT(fidl::Equals(item.format_details(), initial_input_format_details_));

      latest_input_format_details_ = fidl::Clone(item.format_details());

      is_input_format_details_pending_ = true;
      continue;
    }

    if (item.is_end_of_stream()) {
      result.is_eos = true;
      is_input_end_of_stream_queued_to_core_ = true;
      return result;
    }

    ZX_DEBUG_ASSERT(item.is_packet());

    if (is_input_format_details_pending_) {
      is_input_format_details_pending_ = false;
      std::vector<uint8_t> oob_bytes = ParseCodecOobBytes();
      if (!oob_bytes.empty()) {
        result.length = oob_bytes.size();
        result.data = std::move(oob_bytes);
        // Put packet back for next call to ReadMoreInputData().
        QueueInputItem(std::move(item), true);
        return result;
      }
    }

    fit::deferred_callback return_input_packet = fit::defer_callback(fit::closure(
        [this, packet = item.packet()] { events_->onCoreCodecInputPacketDone(packet); }));

    uint8_t* data = item.packet()->buffer()->base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();
    auto parsed_input_data = ParseVideo(item.packet()->buffer(), &return_input_packet, data, len);
    if (!parsed_input_data) {
      continue;
    }
    result = std::move(parsed_input_data.value());
    if (result.codec_buffer && !IsPortSecure(kInputPort)) {
      // In case input is still dirty in CPU cache.
      ZX_DEBUG_ASSERT(result.length <= std::numeric_limits<uint32_t>::max());
      result.codec_buffer->CacheFlush(result.buffer_start_offset,
                                      static_cast<uint32_t>(result.length));
    }
    if (item.packet()->has_timestamp_ish()) {
      result.pts = item.packet()->timestamp_ish();
    }

    return result;
    // ~item
  }
}

bool CodecAdapterH264Multi::HasMoreInputData() {
  std::lock_guard<std::mutex> lock(lock_);
  return !input_queue_.empty();
}

std::vector<uint8_t> CodecAdapterH264Multi::ParseCodecOobBytes() {
  // Our latest oob_bytes may contain SPS/PPS info.  If we have any
  // such info, the core codec needs it (possibly converted first).

  // If there's no OOB info, then there's nothing to do, as all such info will
  // be in-band in normal packet-based AnnexB NALs (including start codes and
  // start code emulation prevention bytes).
  if (!latest_input_format_details_.has_oob_bytes() ||
      latest_input_format_details_.oob_bytes().empty()) {
    // success
    return {};
  }

  const std::vector<uint8_t>* oob = &latest_input_format_details_.oob_bytes();

  // We need to deliver Annex B style SPS/PPS to this core codec, regardless of
  // what format the oob_bytes is in.

  // The oob_bytes can be in two different forms, which can be detected by
  // the value of the first byte:
  //
  // 0 - Annex B form already.  The 0 is the first byte of a start code.
  // 1 - AVCC form, which we'll convert to Annex B form.  AVCC version 1.  There
  //   is no AVCC version 0.
  // anything else - fail.
  //
  // In addition, we need to know if AVCC or not since we need to know whether
  // to add start code emulation prevention bytes or not.  And if it's AVCC,
  // how many bytes long the pseudo_nal_length field is - that field is before
  // each input NAL.

  // We already checked empty() above.
  ZX_DEBUG_ASSERT(oob->size() >= 1);
  switch ((*oob)[0]) {
    case 0:
      is_avcc_ = false;
      return *oob;
    case 1: {
      // This applies to both the oob data and the input packet payload data.
      // Both are AVCC, or both are AnnexB.
      is_avcc_ = true;

      /*
        AVCC OOB data layout (bits):
        [0] (8) - version 1
        [1] (8) - h264 profile #
        [2] (8) - compatible profile bits
        [3] (8) - h264 level (eg. 31 == "3.1")
        [4] (6) - reserved, can be set to all 1s
            (2) - pseudo_nal_length_field_bytes_ - 1
        [5] (3) - reserved, can be set to all 1s
            (5) - sps_count
              (16) - sps_bytes
              (8*sps_bytes) - SPS nal_unit_type (that byte) + SPS data as RBSP.
            (8) - pps_count
              (16) - pps_bytes
              (8*pps_bytes) - PPS nal_unit_type (that byte) + PPS data as RBSP.
      */

      // We accept 0 SPS and/or 0 PPS, but typically there's one of each.  At
      // minimum the oob buffer needs to be large enough to contain both the
      // sps_count and pps_count fields, which is a min of 7 bytes.
      if (oob->size() < 7) {
        LOG(ERROR, "oob->size() < 7");
        OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
        return {};
      }
      // All pseudo-NALs in input packet payloads will use the
      // parsed count of bytes of the length field. Convert SPS/PPS inline to AnnexB format so we
      // can return it directly, as ParseVideo won't be called on this data.
      pseudo_nal_length_field_bytes_ = ((*oob)[4] & 0x3) + 1;
      uint32_t sps_count = (*oob)[5] & 0x1F;
      uint32_t offset = 6;
      std::vector<uint8_t> accumulation;
      for (uint32_t i = 0; i < sps_count; ++i) {
        if (offset + 2 > oob->size()) {
          LOG(ERROR, "offset + 2 > oob->size()");
          OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return {};
        }
        uint32_t sps_length = (*oob)[offset] * 256 + (*oob)[offset + 1];
        if (offset + 2 + sps_length > oob->size()) {
          LOG(ERROR, "offset + 2 + sps_length > oob->size()");
          OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return {};
        }
        offset += 2;  // sps_bytes
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(1);
        for (uint32_t i = 0; i < sps_length; i++) {
          accumulation.push_back(oob->data()[offset + i]);
        }
        offset += sps_length;
      }
      if (offset + 1 > oob->size()) {
        LOG(ERROR, "offset + 1 > oob->size()");
        OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
        return {};
      }
      uint32_t pps_count = (*oob)[offset++];
      for (uint32_t i = 0; i < pps_count; ++i) {
        if (offset + 2 > oob->size()) {
          LOG(ERROR, "offset + 2 > oob->size()");
          OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return {};
        }
        uint32_t pps_length = (*oob)[offset] * 256 + (*oob)[offset + 1];
        if (offset + 2 + pps_length > oob->size()) {
          LOG(ERROR, "offset + 2 + pps_length > oob->size()");
          OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return {};
        }
        offset += 2;  // pps_bytes
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(1);
        for (uint32_t i = 0; i < pps_length; i++) {
          accumulation.push_back(oob->data()[offset + i]);
        }
        offset += pps_length;
      }
      return accumulation;
    }
    default:
      LOG(ERROR, "unexpected first oob byte");
      OnCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
      return {};
  }
}

std::optional<H264MultiDecoder::DataInput> CodecAdapterH264Multi::ParseVideo(
    const CodecBuffer* buffer, fit::deferred_callback* return_input_packet, const uint8_t* data,
    uint32_t length) {
  if (is_avcc_) {
    return ParseVideoAvcc(data, length);
    // ~return_input_packet
  } else {
    return ParseVideoAnnexB(buffer, return_input_packet, data, length);
  }
}

std::optional<H264MultiDecoder::DataInput> CodecAdapterH264Multi::ParseVideoAvcc(
    const uint8_t* data, uint32_t length) {
  // We don't necessarily know that is_avcc_ is true on entry to this method.
  // We use this method to send the decoder a bunch of 0x00 sometimes, which
  // will call this method regardless of is_avcc_ or not.

  // So far, the "avcC"/"AVCC" we've seen has emulation prevention bytes on it
  // already.  So we don't add those here.  But if we did need to add them, we'd
  // add them here.

  // For now we assume the heap is pretty fast and doesn't mind the size thrash,
  // but maybe we'll want to keep a buffer around (we'll optimize only if/when
  // we determine this is actually a problem).  We only actually use this buffer
  // if is_avcc_ (which is not uncommon).

  // We do parse more than one pseudo_nal per input packet.
  //
  // No splitting NALs across input packets, for now.
  //
  // TODO(dustingreen): Allow splitting NALs across input packets (not a small
  // change).  Probably also move into a source_set for sharing with other
  // CodecAdapter(s).

  // Count the input pseudo_nal(s)
  uint32_t pseudo_nal_count = 0;
  uint32_t i = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      LOG(ERROR, "i + pseudo_nal_length_field_bytes_ > length");
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    // Read pseudo_nal_length field, which is a field which can be 1-4 bytes
    // long because AVCC/avcC.
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_; ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      LOG(ERROR, "i + pseudo_nal_length > length");
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    i += pseudo_nal_length;
    ++pseudo_nal_count;
  }

  static constexpr uint32_t kStartCodeBytes = 4;
  uint32_t local_length = length - pseudo_nal_count * pseudo_nal_length_field_bytes_ +
                          pseudo_nal_count * kStartCodeBytes;
  auto local_buffer = std::make_unique<uint8_t[]>(local_length);
  uint8_t* local_data = local_buffer.get();

  i = 0;
  uint32_t o = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      LOG(ERROR, "i + pseudo_nal_length_field_bytes_ > length");
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_; ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      LOG(ERROR, "i + pseudo_nal_length > length");
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }

    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 1;

    memcpy(&local_data[o], &data[i], pseudo_nal_length);
    o += pseudo_nal_length;
    i += pseudo_nal_length;
  }
  ZX_DEBUG_ASSERT(o == local_length);
  ZX_DEBUG_ASSERT(i == length);

  return ParseVideoAnnexB(nullptr, nullptr, local_data, local_length);
}

std::optional<H264MultiDecoder::DataInput> CodecAdapterH264Multi::ParseVideoAnnexB(
    const CodecBuffer* buffer, fit::deferred_callback* return_input_packet, const uint8_t* data,
    uint32_t length) {
  ZX_DEBUG_ASSERT(data);
  ZX_DEBUG_ASSERT(!!buffer == !!return_input_packet);
  H264MultiDecoder::DataInput result;
  result.length = length;
  if (!buffer) {
    result.data = std::vector<uint8_t>(data, data + length);
  } else {
    ZX_DEBUG_ASSERT(buffer);
    // Caller is required to ensure that data is within [base()..base()+size()).
    ZX_DEBUG_ASSERT(data >= buffer->base());
    ZX_DEBUG_ASSERT(data < buffer->base() + buffer->size());
    ZX_DEBUG_ASSERT(data - buffer->base() <= std::numeric_limits<uint32_t>::max());
    ZX_DEBUG_ASSERT(return_input_packet);
    result.codec_buffer = buffer;
    result.buffer_start_offset = static_cast<uint32_t>(data - buffer->base());
    result.return_input_packet = std::move(*return_input_packet);
  }
  return std::move(result);
}

void CodecAdapterH264Multi::OnEos() { events_->onCoreCodecOutputEndOfStream(false); }

zx_status_t CodecAdapterH264Multi::InitializeFrames(uint32_t min_frame_count,
                                                    uint32_t max_frame_count, uint32_t coded_width,
                                                    uint32_t coded_height, uint32_t stride,
                                                    uint32_t display_width, uint32_t display_height,
                                                    bool has_sar, uint32_t sar_width,
                                                    uint32_t sar_height) {
  // This is called on a core codec thread, ordered with respect to emitted
  // output frames.
  //
  // Frame initialization is async.
  //
  // If existing buffers are suitable, we can init / re-init frames without
  // re-allocating buffers, so we don't need the client's help (or sysmem's),
  // and it's faster.
  //
  // If existing buffers are not suitable, this completes when either the client
  // has configured output buffers and we've done core codec
  // InitializedFrames(), or until the cilent has moved on by closing the
  // current stream.
  //
  // The video_decoder_lock_ is held during this method.
  //
  // First stash some format and buffer count info needed to initialize frames
  // before triggering re-init of frames / mid-stream format change.  Later,
  // frames satisfying these stashed parameters will be handed to the decoder
  // via InitializedFrames(), unless CoreCodecStopStream() happens first.
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);

    min_buffer_count_[kOutputPort] = min_frame_count;
    max_buffer_count_[kOutputPort] = max_frame_count;
    width_ = coded_width;
    height_ = coded_height;
    min_stride_ = stride;
    display_width_ = display_width;
    display_height_ = display_height;
    has_sar_ = has_sar;
    sar_width_ = sar_width;
    sar_height_ = sar_height;
  }  // ~lock

  // After a stream switch, the new H264MultiDecoder won't have any frames, and needs
  // InitializedFrames() to get called to set up the frames, and won't have checked
  // IsCurrentOutputBufferCollectionUsable() itself since it knows it needs frames configured using
  // InitializedFrames().  However, we can still check here whether the current buffer collection,
  // that was (before the stream switch) used with a previous H264MultiDecoder instance, can still
  // be used with the new H264MultiDecoder instance.  This does require that we be able to indicate
  // via InitializedFrames() which frames are presently usable vs. which are still downstream and
  // not yet returned.
  if (IsCurrentOutputBufferCollectionUsable(min_frame_count, max_frame_count, coded_width,
                                            coded_height, stride, display_width, display_height)) {
    DLOG("IsCurrentOutputBufferCollectionUsable() true");
    // The core codec won't output any more packets until we call InitializedFrames(), but when the
    // core codec does output more packets, we need to send updated format info first.
    //
    // TODO(dustingreen): This may be unnecessary / redundant.
    events_->onCoreCodecOutputFormatChange();
    shared_fidl_thread_closure_queue_->Enqueue([this] {
      // We have to run this on the shared fidl thread since that's what CodecImpl is using to
      // process RecycleOutputPacket(); we need to avoid this running concurrently with
      // CoreCodecRecycleOutputPacket().
      MidStreamOutputBufferConfigInternal(false);
    });
    return ZX_OK;
  }

  // If we don't have a current output BufferCollection or can't re-use it due to unsuitable
  // constraints, we need to trigger a mid-stream output constraints change to trigger a new
  // BufferCollection to be allocated that's consistent with the new constraints.
  //
  // This will snap the current stream_lifetime_ordinal_, and call
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and
  // CoreCodecMidStreamOutputBufferReConfigFinish() from the StreamControl
  // thread, _iff_ the client hasn't already moved on to a new stream by then.
  events_->onCoreCodecMidStreamOutputConstraintsChange(true);

  return ZX_OK;
}

bool CodecAdapterH264Multi::IsCurrentOutputBufferCollectionUsable(
    uint32_t min_frame_count, uint32_t max_frame_count, uint32_t coded_width, uint32_t coded_height,
    uint32_t stride, uint32_t display_width, uint32_t display_height) {
  DLOG(
      "min_frame_count: %u max_frame_count: %u coded_width: %u coded_height: %u stride: %u "
      "display_width: %u display_height: %u",
      min_frame_count, max_frame_count, coded_width, coded_height, stride, display_width,
      display_height);
  ZX_DEBUG_ASSERT(stride >= coded_width);
  // We don't ask codec_impl about this, because as far as codec_impl is
  // concerned, the output buffer collection might not be used for video
  // frames.  We could have common code for video decoders but for now we just
  // check here.
  //
  // TODO(dustingreen): Some potential divisor check failures could be avoided
  // if the corresponding value were rounded up according to the divisor before
  // we get here.
  if (!output_buffer_collection_info_) {
    LOG(DEBUG, "!output_buffer_collection_info_");
    return false;
  }
  fuchsia::sysmem::BufferCollectionInfo_2& info = output_buffer_collection_info_.value();
  ZX_DEBUG_ASSERT(info.settings.has_image_format_constraints);
  if (min_frame_count > info.buffer_count) {
    LOG(DEBUG, "min_frame_count > info.buffer_count");
    return false;
  }
  if (min_frame_count > min_buffer_count_[kOutputPort]) {
    LOG(DEBUG, "min_frame_count > min_buffer_count_[kOutputPort]");
    return false;
  }
  if (info.buffer_count > max_frame_count) {
    // The h264_multi_decoder.cc won't exercise this path since the max is always the same, and we
    // won't have allocated a collection with more than max_buffer_count.
    LOG(DEBUG, "info.buffer_count > max_frame_count");
    return false;
  }
  if (stride * coded_height * 3 / 2 > info.settings.buffer_settings.size_bytes) {
    LOG(DEBUG, "stride * coded_height * 3 / 2 > info.settings.buffer_settings.size_bytes");
    return false;
  }
  if (display_width % info.settings.image_format_constraints.display_width_divisor != 0) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG,
        "display_width %% info.settings.image_format_constraints.display_width_divisor != 0");
    return false;
  }
  if (display_height % info.settings.image_format_constraints.display_height_divisor != 0) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG,
        "display_height %% info.settings.image_format_constraints.display_height_divisor != 0");
    return false;
  }
  if (coded_width * coded_height >
      info.settings.image_format_constraints.max_coded_width_times_coded_height) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG, "coded_width * coded_height > max_coded_width_times_coded_height");
    return false;
  }

  if (coded_width < info.settings.image_format_constraints.min_coded_width) {
    LOG(DEBUG,
        "coded_width < info.settings.image_format_constraints.min_coded_width -- "
        "coded_width: %d min_coded_width: %d",
        coded_width, info.settings.image_format_constraints.min_coded_width);
    return false;
  }
  if (coded_width > info.settings.image_format_constraints.max_coded_width) {
    LOG(DEBUG, "coded_width > info.settings.image_format_constraints.max_coded_width");
    return false;
  }
  if (coded_width % info.settings.image_format_constraints.coded_width_divisor != 0) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG, "coded_width %% info.settings.image_format_constraints.coded_width_divisor != 0");
    return false;
  }
  if (coded_height < info.settings.image_format_constraints.min_coded_height) {
    LOG(DEBUG, "coded_height < info.settings.image_format_constraints.min_coded_height");
    return false;
  }
  if (coded_height > info.settings.image_format_constraints.max_coded_height) {
    LOG(DEBUG, "coded_height > info.settings.image_format_constraints.max_coded_height");
    return false;
  }
  if (coded_height % info.settings.image_format_constraints.coded_height_divisor != 0) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG, "coded_height %% info.settings.image_format_constraints.coded_height_divisor != 0");
    return false;
  }
  if (stride < info.settings.image_format_constraints.min_bytes_per_row) {
    LOG(DEBUG,
        "stride < info.settings.image_format_constraints.min_bytes_per_row -- stride: %d "
        "min_bytes_per_row: %d",
        stride, info.settings.image_format_constraints.min_bytes_per_row);
    return false;
  }
  if (stride > info.settings.image_format_constraints.max_bytes_per_row) {
    LOG(DEBUG, "stride > info.settings.image_format_constraints.max_bytes_per_row");
    return false;
  }
  if (stride % info.settings.image_format_constraints.bytes_per_row_divisor != 0) {
    // Let it probably fail later when trying to re-negotiate buffers.
    LOG(DEBUG, "stride %% info.settings.image_format_constraints.bytes_per_row_divisor != 0");
    return false;
  }

  DLOG("returning true");
  return true;
}

void CodecAdapterH264Multi::AsyncPumpDecoder() {
  async::PostTask(core_loop_.dispatcher(), [this] {
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    if (!decoder_)
      return;
    // Something else may have come along since InitializedFrames and pumped the decoder, but that's
    // ok.
    decoder_->PumpOrReschedule();
  });
}

void CodecAdapterH264Multi::AsyncResetStreamAfterCurrentFrame() {
  LOG(ERROR, "async reset stream (after current frame) triggered");
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // The current stream is temporarily failed, until CoreCodecResetStreamAfterCurrentFrame() soon
    // on the StreamControl thread.  This prevents ReadMoreInputData() from queueing any more input
    // data after any currently-running iteration.
    //
    // While Vp9Decoder::needs_more_input_data() may already be returning false which may serve a
    // similar purpose depending on how/when Vp9Decoder calls this method, it's nice to directly
    // mute queing any more input in this layer.
    is_stream_failed_ = true;
  }  // ~lock
  events_->onCoreCodecResetStreamAfterCurrentFrame();
}

void CodecAdapterH264Multi::CoreCodecResetStreamAfterCurrentFrame() {
  // Currently this takes ~20-40ms per reset.  We might be able to improve the performance by having
  // a stop that doesn't deallocate followed by a start that doesn't allocate, but since we'll
  // fairly soon only be using this method during watchdog processing, it's not worth optimizing for
  // the temporary time interval during which we might potentially use this on multiple
  // non-keyframes in a row before a keyframe, only in the case of protected input.
  //
  // If we were to optimize in that way, it'd increase the complexity of init and de-init code.  The
  // current way we use that code exactly the same way for reset as for init and de-init, which is
  // good from a test coverage point of view.

  // This fences and quiesces the input processing thread, and the StreamControl thread (current
  // thread) is the only other thread that modifies is_input_end_of_stream_queued_to_core_, so we
  // know is_input_end_of_stream_queued_to_core_ won't be changing.
  LOG(DEBUG, "before CoreCodecStopStreamInternal()");
  std::list<CodecInputItem> input_items = CoreCodecStopStreamInternal();
  auto return_any_input_items = fit::defer([this, &input_items] {
    for (auto& input_item : input_items) {
      if (input_item.is_packet()) {
        events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
      }
    }
  });

  if (is_input_end_of_stream_queued_to_core_) {
    // We don't handle this corner case of a corner case.  Fail the stream instead.
    events_->onCoreCodecFailStream(fuchsia::media::StreamError::EOS_PROCESSING);
    return;
  }

  LOG(DEBUG, "after stop; before CoreCodecStartStream()");

  CoreCodecStartStream();

  LOG(DEBUG, "re-queueing items...");
  while (!input_items.empty()) {
    QueueInputItem(std::move(input_items.front()));
    input_items.pop_front();
  }
  LOG(DEBUG, "done re-queueing items.");
}

void CodecAdapterH264Multi::CoreCodecSetStreamControlProfile(
    zx::unowned_thread stream_control_thread) {
  device_->SetThreadProfile(std::move(stream_control_thread), ThreadRole::kH264MultiStreamControl);
}

void CodecAdapterH264Multi::OnCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  LOG(INFO, "calling events_->onCoreCodecFailStream(): %u", static_cast<uint32_t>(error));
  events_->onCoreCodecFailStream(error);
}

CodecPacket* CodecAdapterH264Multi::GetFreePacket(const CodecBuffer* buffer) {
  std::lock_guard<std::mutex> lock(lock_);
  // The h264 decoder won't repeatedly output a buffer multiple times
  // concurrently, so a free buffer (for which the caller needs a packet)
  // implies a free packet.
  ZX_DEBUG_ASSERT(!free_output_packets_.empty());
  uint32_t free_index = free_output_packets_.back();
  free_output_packets_.pop_back();
  CodecPacket* packet = all_output_packets_[free_index];
  // Associate the buffer with the packet while the packet is in-flight.  We don't strictly need to
  // be doing this under lock_, but doesn't hurt, and it's easier to understand how things work with
  // this under lock_.
  packet->SetBuffer(buffer);
  return packet;
}

bool CodecAdapterH264Multi::IsPortSecureRequired(CodecPort port) {
  return secure_memory_mode_[port] == fuchsia::mediacodec::SecureMemoryMode::ON;
}

bool CodecAdapterH264Multi::IsPortSecurePermitted(CodecPort port) {
  return secure_memory_mode_[port] != fuchsia::mediacodec::SecureMemoryMode::OFF;
}

bool CodecAdapterH264Multi::IsPortSecure(CodecPort port) {
  ZX_DEBUG_ASSERT(buffer_settings_[port]);
  return buffer_settings_[port]->buffer_settings.is_secure;
}

bool CodecAdapterH264Multi::IsOutputSecure() {
  // We need to know whether output is secure or not before we start accepting input, which means
  // we need to know before output buffers are allocated, which means we can't rely on the result
  // of sysmem BufferCollection allocation is_secure for output.
  ZX_DEBUG_ASSERT(IsPortSecurePermitted(kOutputPort) == IsPortSecureRequired(kOutputPort));
  return IsPortSecureRequired(kOutputPort);
}

}  // namespace amlogic_decoder
