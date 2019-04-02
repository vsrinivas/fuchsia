// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vp9.h"

#include "device_ctx.h"
#include "hevcdec.h"
#include "pts_manager.h"
#include "vp9_decoder.h"
#include "vp9_utils.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/zx/bti.h>

// TODO(dustingreen):
//   * Split InitializeStream() into two parts, one to get the format info from
//     the HW and send it to the Codec client, the other part to configure
//     output buffers once the client has configured Codec output config based
//     on the format info.  Wire up so that
//     onCoreCodecMidStreamOutputConstraintsChange() gets called and so that
//     CoreCodecBuildNewOutputConstraints() will pick up the correct current format
//     info (whether still mid-stream, or at the start of a new stream that's
//     starting before the mid-stream format change was processed for the old
//     stream).
//   * Allocate output video buffers contig by setting relevant buffer
//     constraints to indicate contig to BufferAllocator / BufferCollection.
//   * On EndOfStream at input, push all remaining data through the HW decoder
//     and detect when the EndOfStream is appropriate to generate at the output.
//   * Split video_->Parse() into start/complete and/or switch to feeding the
//     ring buffer directly, or whatever is wanted by multi-concurrent-stream
//     mode.
//   * Consider if there's a way to get AmlogicVideo to re-use buffers across
//     a stream switch without over-writing buffers that are still in-use
//     downstream.

namespace {

// avconv -f lavfi -i color=c=black:s=42x52 -c:v vp9 -vframes 1 new_stream.ivf
//
// xxd -i new_stream.ivf
//
// We push this through the decoder as our "EndOfStream" marker, and detect it
// at the output (for now) by its unusual 42x52 resolution during
// InitializeStream() _and_ the fact that we've queued this marker.  To force
// this frame to be handled by the decoder we queue kFlushThroughBytes of 0
// after this data.
//
// TODO(dustingreen): We don't currently detect the EndOfStream via its stream
// offset in PtsManager (for vp9), but that would be marginally more robust
// than detecting the special resolution.  However, to detect via stream offset,
// we'd either need to avoid switching resolutions, or switch resolutions using
// the same output buffer set (including preserving the free/busy status of each
// buffer across the boundary), and delay notifying the client until we're sure
// a format change is real, not just the one immediately before a frame whose
// stream offset is >= the EndOfStream offset.

unsigned char new_stream_ivf[] = {
    0x44, 0x4b, 0x49, 0x46, 0x00, 0x00, 0x20, 0x00, 0x56, 0x50, 0x39,
    0x30, 0x2a, 0x00, 0x34, 0x00, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1e,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x82, 0x49, 0x83, 0x42, 0x00, 0x02, 0x90, 0x03, 0x36, 0x00, 0x38,
    0x24, 0x1c, 0x18, 0x54, 0x00, 0x00, 0x30, 0x60, 0x00, 0x00, 0x13,
    0xbf, 0xff, 0xfd, 0x15, 0x62, 0x00, 0x00, 0x00};
unsigned int new_stream_ivf_len = 74;
constexpr uint32_t kHeaderSkipBytes = 32 + 12;  // Skip IVF headers.
constexpr uint32_t kFlushThroughBytes = 16384;
constexpr uint32_t kEndOfStreamWidth = 42;
constexpr uint32_t kEndOfStreamHeight = 52;

// Zero-initialized, so it shouldn't take up space on-disk.
const uint8_t kFlushThroughZeroes[kFlushThroughBytes] = {};

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

}  // namespace

CodecAdapterVp9::CodecAdapterVp9(std::mutex& lock,
                                 CodecAdapterEvents* codec_adapter_events,
                                 DeviceCtx* device)
    : CodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  ZX_DEBUG_ASSERT(device_);
  ZX_DEBUG_ASSERT(video_);
}

CodecAdapterVp9::~CodecAdapterVp9() {
  // TODO(dustingreen): Remove the printfs or switch them to VLOG.
  input_processing_loop_.Quit();
  input_processing_loop_.JoinThreads();
  input_processing_loop_.Shutdown();

  // nothing else to do here, at least not until we aren't calling PowerOff() in
  // CoreCodecStopStream().
}

bool CodecAdapterVp9::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

bool CodecAdapterVp9::IsCoreCodecMappedBufferNeeded(CodecPort port) {
  // If buffers are protected, the decoder should/will call secmem TA to re-pack
  // VP9 headers in the input.  Else the decoder will use a CPU mapping to do
  // this repack.
  //
  // TODO(dustingreen): Make the previous paragraph true.  For now we have to
  // re-pack using the CPU on REE side.
  return true;
}

bool CodecAdapterVp9::IsCoreCodecHwBased() {
  return true;
}

void CodecAdapterVp9::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  zx_status_t result = input_processing_loop_.StartThread(
      "CodecAdapterVp9::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterVp9::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);

  // TODO(dustingreen): We do most of the setup in CoreCodecStartStream()
  // currently, but we should do more here and less there.
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterVp9::CoreCodecGetBufferCollectionConstraints(
    CodecPort port,
    const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  ZX_ASSERT_MSG(false, "not yet implemented");
  return fuchsia::sysmem::BufferCollectionConstraints();
}

void CodecAdapterVp9::CoreCodecSetBufferCollectionInfo(
    CodecPort port,
    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.coherency_domain == fuchsia::sysmem::CoherencyDomain::Cpu);
  if (port == kOutputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.has_image_format_constraints);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.image_format_constraints.pixel_format.type == fuchsia::sysmem::PixelFormatType::NV12);
  }
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterVp9::CoreCodecStartStream() {
  zx_status_t status;

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    parsed_video_size_ = 0;
    is_input_end_of_stream_queued_ = false;
    is_stream_failed_ = false;
  }  // ~lock

  auto decoder = std::make_unique<Vp9Decoder>(
      video_, Vp9Decoder::InputType::kMultiFrameBased);
  decoder->SetFrameDataProvider(this);
  decoder->SetFrameReadyNotifier([this](std::shared_ptr<VideoFrame> frame) {
    // The Codec interface requires that emitted frames are cache clean
    // at least for now.  We invalidate without skipping over stride-width
    // per line, at least partly because stride - width is small (possibly
    // always 0) for this decoder.  But we do invalidate the UV section
    // separately in case uv_plane_offset happens to leave significant
    // space after the Y section (regardless of whether there's actually
    // ever much padding there).
    //
    // TODO(dustingreen): Probably there's not ever any significant
    // padding between Y and UV for this decoder, so probably can make one
    // invalidate call here instead of two with no downsides.
    //
    // TODO(dustingreen): Skip this when the buffer isn't map-able.
    io_buffer_cache_flush_invalidate(&frame->buffer, 0,
                                     frame->stride * frame->height);
    io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                     frame->stride * frame->height / 2);

    const CodecBuffer* buffer = frame->codec_buffer;
    ZX_DEBUG_ASSERT(buffer);

    CodecPacket* packet = GetFreePacket();
    // We know there will be a free packet thanks to SetCheckOutputReady().
    ZX_DEBUG_ASSERT(packet);

    packet->SetBuffer(buffer);
    packet->SetStartOffset(0);
    uint64_t total_size_bytes = frame->stride * frame->height * 3 / 2;
    packet->SetValidLengthBytes(total_size_bytes);

    if (frame->has_pts) {
      packet->SetTimstampIsh(frame->pts);
    } else {
      packet->ClearTimestampIsh();
    }

    events_->onCoreCodecOutputPacket(packet, false, false);
  });
  decoder->SetInitializeFramesHandler(
      fit::bind_member(this, &CodecAdapterVp9::InitializeFramesHandler));
  decoder->SetErrorHandler([this] { OnCoreCodecFailStream(); });
  decoder->SetCheckOutputReady([this] {
    std::lock_guard<std::mutex> lock(lock_);
    // We're ready if output hasn't been configured yet, or if we have free
    // output packets.  This way the decoder can swap in when there's no output
    // config yet, but will stop trying to run when we're out of output packets.
    return all_output_packets_.empty() || !free_output_packets_.empty();
  });

  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    status = decoder->InitializeBuffers();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec(
          "video_->video_decoder_->Initialize() failed");
      return;
    }

    auto instance = std::make_unique<DecoderInstance>(std::move(decoder),
                                                      video_->hevc_core());
    status = video_->AllocateStreamBuffer(instance->stream_buffer(),
                                          512 * PAGE_SIZE);
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("AllocateStreamBuffer() failed");
      return;
    }

    decoder_ = static_cast<Vp9Decoder*>(instance->decoder());
    video_->AddNewDecoderInstance(std::move(instance));
    // Decoder is currently swapped out, but will be swapped in when data is
    // received for it.
  }  // ~lock
}

void CodecAdapterVp9::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the FormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterVp9::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterVp9::CoreCodecQueueInputEndOfStream() {
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
void CodecAdapterVp9::CoreCodecStopStream() {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This helps any previously-queued ProcessInput() calls return faster.
    is_cancelling_input_processing_ = true;
    std::condition_variable stop_input_processing_condition;
    // We know there won't be any new queuing of input, so once this posted work
    // runs, we know all previously-queued ProcessInput() calls have returned.
    PostToInputProcessingThread([this, &stop_input_processing_condition] {
      std::list<CodecInputItem> leftover_input_items;
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        ZX_DEBUG_ASSERT(is_cancelling_input_processing_);
        leftover_input_items = std::move(input_queue_);
        is_cancelling_input_processing_ = false;
      }  // ~lock
      for (auto& input_item : leftover_input_items) {
        if (input_item.is_packet()) {
          events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
        }
      }
      stop_input_processing_condition.notify_all();
    });
    while (is_cancelling_input_processing_) {
      stop_input_processing_condition.wait(lock);
    }
    ZX_DEBUG_ASSERT(!is_cancelling_input_processing_);
  }  // ~lock

  // TODO(dustingreen): Currently, we have to tear down a few pieces of video_,
  // to make it possible to run all the AmlogicVideo + DecoderCore +
  // VideoDecoder code that seems necessary to run to ensure that a new stream
  // will be entirely separate from an old stream, without deleting/creating
  // AmlogicVideo itself.  Probably we can tackle this layer-by-layer, fixing up
  // AmlogicVideo to be more re-usable without the stuff in this method, then
  // DecoderCore, then VideoDecoder.

  if (decoder_) {
    Vp9Decoder* decoder_to_remove = decoder_;
    // We care that decoder_ = nullptr under the lock before it becomes bad to
    // call ReturnFrame() in CoreCodecRecycleOutputPacket().  The two sequential
    // lock hold intervals of video_decoder_lock() don't need to be one
    // interval.
    {  // scope lock
      std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
      decoder_ = nullptr;
    }
    // If the decoder's still running this will stop it as well.
    video_->RemoveDecoder(decoder_to_remove);
  }
}

void CodecAdapterVp9::CoreCodecAddBuffer(CodecPort port,
                                         const CodecBuffer* buffer) {
  all_output_buffers_.push_back(buffer);
}

void CodecAdapterVp9::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port == kOutputPort) {
    ZX_DEBUG_ASSERT(all_output_packets_.empty());
    ZX_DEBUG_ASSERT(free_output_packets_.empty());
    ZX_DEBUG_ASSERT(!all_output_buffers_.empty());
    ZX_DEBUG_ASSERT(all_output_buffers_.size() == packets.size());
    for (auto& packet : packets) {
      all_output_packets_.push_back(packet.get());
      free_output_packets_.push_back(packet.get()->packet_index());
    }
    // This should prevent any inadvetent dependence by clients on the ordering
    // of packet_index values in the output stream or any assumptions re. the
    // relationship between packet_index and buffer_index.
    std::shuffle(free_output_packets_.begin(), free_output_packets_.end(),
                 not_for_security_prng_);
  }
}

void CodecAdapterVp9::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  ZX_DEBUG_ASSERT(!packet->is_new());

  const CodecBuffer* buffer = packet->buffer();
  packet->SetBuffer(nullptr);

  // Getting the buffer is all we needed the packet for, so note that the packet
  // is free fairly early, to side-step any issues with early returns.  The
  // CodecImpl already considers the packet free, but it won't actually get
  // re-used until after it goes on the free list here.
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    free_output_packets_.push_back(packet->packet_index());
  }  // ~lock

  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    std::shared_ptr<VideoFrame> frame = buffer->video_frame().lock();
    if (!frame) {
      // EndOfStream seen at the output, or a new InitializeFrames(), can cause
      // !frame, which is fine.  In that case, any new stream will request
      // allocation of new frames.
      return;
    }
    // Recycle can happen while stopped, but this CodecAdapater has no way yet
    // to return frames while stopped, or to re-use buffers/frames across a
    // stream switch.  Any new stream will request allocation of new frames.
    if (!decoder_) {
      return;
    }
    decoder_->ReturnFrame(frame);
    video_->TryToReschedule();
  }  // ~lock
}

void CodecAdapterVp9::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
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
  }
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterVp9::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  // bear.vp9 decodes into 320x192 YUV buffers, but the video display
  // dimensions are 320x180.  A the bottom of the buffer only .25 of the last
  // 16 height macroblock row is meant to be displayed.
  //
  // TODO(dustingreen): Need to plumb video size separately from buffer size so
  // we can display (for example) a video at 320x180 instead of the buffer's
  // 320x192.  The extra pixels look like don't-care pixels that just let
  // themselves float essentially (re. past-the-boundary behavior of those
  // pixels).  Such pixels aren't meant to be displayed and look strange.
  // Presumably the difference is the buffer needing to be a whole macroblock in
  // width/height (%16==0) vs. the video dimensions being allowed to not use all
  // of the last macroblock.
  //
  // This decoder produces NV12.

  // For the moment, this codec splits 16 into 14 for the codec and 2 for the
  // client.
  //
  // TODO(dustingreen): Plumb actual frame counts.
  constexpr uint32_t kPacketCountForClientForced = 2;
  // Fairly arbitrary.  The client should set a higher value if the client needs
  // to camp on more frames than this.
  constexpr uint32_t kDefaultPacketCountForClient = kPacketCountForClientForced;

  uint32_t per_packet_buffer_bytes = stride_ * height_ * 3 / 2;

  auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();
  auto* default_settings = constraints->mutable_default_settings();

  // For the moment, there will be only one StreamOutputConstraints, and it'll need
  // output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(
      buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // 0 is intentionally invalid - the client must fill out this field.
  default_settings->set_buffer_lifetime_ordinal(0);
  default_settings->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);
  default_settings->set_packet_count_for_server(packet_count_total_ -
                                                kPacketCountForClientForced);
  default_settings->set_packet_count_for_client(kDefaultPacketCountForClient);
  // Packed NV12 (no extra padding, min UV offset, min stride).
  default_settings->set_per_packet_buffer_bytes(per_packet_buffer_bytes);
  default_settings->set_single_buffer_mode(false);

  // For the moment, let's just force the client to allocate this exact size.
  constraints->set_per_packet_buffer_bytes_min(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_recommended(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_max(per_packet_buffer_bytes);

  // For the moment, let's just force the client to set this exact number of
  // frames for the codec.
  constraints->set_packet_count_for_server_min(packet_count_total_ -
                                               kPacketCountForClientForced);
  constraints->set_packet_count_for_server_recommended(
      packet_count_total_ - kPacketCountForClientForced);
  constraints->set_packet_count_for_server_recommended_max(
      packet_count_total_ - kPacketCountForClientForced);
  constraints->set_packet_count_for_server_max(packet_count_total_ -
                                               kPacketCountForClientForced);

  constraints->set_packet_count_for_client_min(kPacketCountForClientForced);
  constraints->set_packet_count_for_client_max(kPacketCountForClientForced);

  // False because it's not required and not encouraged for a video decoder
  // output to allow single buffer mode.
  constraints->set_single_buffer_mode_allowed(false);

  constraints->set_is_physically_contiguous_required(true);
  ::zx::bti very_temp_kludge_bti;
  zx_status_t dup_status =
      ::zx::unowned<::zx::bti>(video_->bti())
          ->duplicate(ZX_RIGHT_SAME_RIGHTS, &very_temp_kludge_bti);
  if (dup_status != ZX_OK) {
    events_->onCoreCodecFailCodec("BTI duplicate failed - status: %d",
                                  dup_status);
    return nullptr;
  }
  // This is very temporary.  The BufferAllocator should handle this directly,
  // not the client.
  constraints->set_very_temp_kludge_bti_handle(std::move(very_temp_kludge_bti));

  return config;
}

fuchsia::media::StreamOutputFormat
CodecAdapterVp9::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_format_details_version_ordinal) {
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
  video_uncompressed.primary_line_stride_bytes = stride_;
  video_uncompressed.secondary_line_stride_bytes = stride_;
  video_uncompressed.primary_start_offset = 0;
  video_uncompressed.secondary_start_offset = stride_ * height_;
  video_uncompressed.tertiary_start_offset = stride_ * height_ + 1;
  video_uncompressed.primary_pixel_stride = 1;
  video_uncompressed.secondary_pixel_stride = 2;
  video_uncompressed.primary_display_width_pixels = display_width_;
  video_uncompressed.primary_display_height_pixels = display_height_;
  video_uncompressed.has_pixel_aspect_ratio = has_sar_;
  video_uncompressed.pixel_aspect_ratio_width = sar_width_;
  video_uncompressed.pixel_aspect_ratio_height = sar_height_;

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(std::move(video_uncompressed));

  result.mutable_format_details()->mutable_domain()->set_video(
      std::move(video_format));

  return result;
}

void CodecAdapterVp9::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, the core codec just needs us to get new frame buffers
  // set up, so nothing to do here.
  //
  // CoreCodecEnsureBuffersNotConfigured() will run soon.
}

void CodecAdapterVp9::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Now that the client has configured output buffers, we need to hand those
  // back to the core codec via return of InitializedFrames

  std::vector<CodecFrame> frames;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // Now we need to populate the frames_out vector.
    for (uint32_t i = 0; i < all_output_buffers_.size(); i++) {
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->buffer_index() == i);
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->codec_buffer().buffer_index() ==
                      i);
      frames.emplace_back(CodecFrame{
          .codec_buffer_spec =
              fidl::Clone(all_output_buffers_[i]->codec_buffer()),
          .codec_buffer_ptr = all_output_buffers_[i],
      });
    }
    width = width_;
    height = height_;
    stride = stride_;
  }  // ~lock
  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    video_->video_decoder()->InitializedFrames(std::move(frames), width, height,
                                               stride);
  }  // ~lock
}

void CodecAdapterVp9::PostSerial(async_dispatcher_t* dispatcher,
                                 fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  ZX_ASSERT_MSG(post_result == ZX_OK, "async::PostTask() failed - result: %d\n",
                post_result);
}

void CodecAdapterVp9::PostToInputProcessingThread(fit::closure to_run) {
  PostSerial(input_processing_loop_.dispatcher(), std::move(to_run));
}

void CodecAdapterVp9::QueueInputItem(CodecInputItem input_item) {
  bool is_trigger_needed = false;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // For now we don't worry about avoiding a trigger if we happen to queue
    // when ProcessInput() has removed the last item but ProcessInput() is still
    // running.
    if (!is_process_input_queued_) {
      is_trigger_needed = input_queue_.empty();
      is_process_input_queued_ = is_trigger_needed;
    }
    input_queue_.emplace_back(std::move(input_item));
  }  // ~lock
  if (is_trigger_needed) {
    PostToInputProcessingThread(
        fit::bind_member(this, &CodecAdapterVp9::ProcessInput));
  }
}

void CodecAdapterVp9::ProcessInput() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_process_input_queued_ = false;
  }  // ~lock
  std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
  auto decoder = static_cast<Vp9Decoder*>(video_->video_decoder());
  if (decoder_ != decoder) {
    video_->TryToReschedule();
    // The reschedule will queue reading input data if this decoder was
    // scheduled.
    return;
  }
  if (decoder->needs_more_input_data()) {
    ReadMoreInputData(decoder);
  }
}

void CodecAdapterVp9::ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) {
  bool is_trigger_needed = false;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // For now we don't worry about avoiding a trigger if we happen to queue
    // when ProcessInput() has removed the last item but ProcessInput() is still
    // running.
    if (!is_process_input_queued_) {
      is_trigger_needed = true;
      is_process_input_queued_ = true;
    }
  }  // ~lock
  // Trigger this on the input thread instead of immediately handling it to
  // simplifying the locking.
  if (is_trigger_needed) {
    PostToInputProcessingThread(
        fit::bind_member(this, &CodecAdapterVp9::ProcessInput));
  }
}

bool CodecAdapterVp9::HasMoreInputData() {
  if (queued_frame_sizes_.size() > 0)
    return true;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ ||
        input_queue_.empty()) {
      return false;
    }
  }  // ~lock
  return true;
}

CodecInputItem CodecAdapterVp9::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ ||
        input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

void CodecAdapterVp9::FrameWasOutput() {
  video_->TryToRescheduleAssumeVideoDecoderLocked();
}

// The decoder lock is held by caller during this method.
void CodecAdapterVp9::ReadMoreInputData(Vp9Decoder* decoder) {
  if (queued_frame_sizes_.size()) {
    decoder->UpdateDecodeSize(queued_frame_sizes_.front());
    queued_frame_sizes_.erase(queued_frame_sizes_.begin());
    return;
  }

  while (true) {
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      return;
    }

    if (item.is_format_details()) {
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      ZX_ASSERT(item.format_details() == initial_input_format_details_);
      continue;
    }

    if (item.is_end_of_stream()) {
      video_->pts_manager()->SetEndOfStreamOffset(parsed_video_size_);
      std::vector<uint8_t> split_data;
      SplitSuperframe(
          reinterpret_cast<const uint8_t*>(&new_stream_ivf[kHeaderSkipBytes]),
          new_stream_ivf_len - kHeaderSkipBytes, &split_data);
      if (ZX_OK !=
          video_->ProcessVideoNoParser(split_data.data(), split_data.size())) {
        OnCoreCodecFailStream();
        return;
      }
      if (ZX_OK != video_->ProcessVideoNoParser(kFlushThroughZeroes,
                                                sizeof(kFlushThroughZeroes))) {
        OnCoreCodecFailStream();
        return;
      }
      // Intentionally not including kFlushThroughZeroes - this only includes
      // data in AMLV frames.
      decoder->UpdateDecodeSize(split_data.size());
      return;
    }

    ZX_DEBUG_ASSERT(item.is_packet());

    uint8_t* data =
        item.packet()->buffer()->buffer_base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

    video_->pts_manager()->InsertPts(parsed_video_size_,
                                     item.packet()->has_timestamp_ish(),
                                     item.packet()->timestamp_ish());
    std::vector<uint8_t> split_data;
    std::vector<uint32_t> new_queued_frame_sizes;
    SplitSuperframe(data, len, &split_data, &new_queued_frame_sizes);

    parsed_video_size_ += split_data.size() + kFlushThroughBytes;

    // If attempting to over-fill the ring buffer, this will fail, currently.
    // That should be rare, since only one superframe will be in the ringbuffer
    // at a time.
    // TODO: Check for short writes and either feed in extra data as space is
    // made or resize the buffer to fit.
    if (ZX_OK !=
        video_->ProcessVideoNoParser(split_data.data(), split_data.size())) {
      OnCoreCodecFailStream();
      return;
    }

    // Always flush through padding before calling UpdateDecodeSize or else the
    // decoder may not see the data because it's stuck in a fifo somewhere and
    // we can get hangs.
    {
      if (ZX_OK != video_->ProcessVideoNoParser(kFlushThroughZeroes,
                                                sizeof(kFlushThroughZeroes))) {
        OnCoreCodecFailStream();
        return;
      }
    }
    queued_frame_sizes_ = std::move(new_queued_frame_sizes);

    if (queued_frame_sizes_.size() == 0) {
      OnCoreCodecFailStream();
      return;
    }
    // Only one frame per superframe should be given at a time, as otherwise the
    // data for frames after that will be thrown away after that first frame is
    // decoded.
    decoder->UpdateDecodeSize(queued_frame_sizes_.front());
    queued_frame_sizes_.erase(queued_frame_sizes_.begin());

    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
    return;
  }
}

zx_status_t CodecAdapterVp9::InitializeFramesHandler(
    ::zx::bti bti, uint32_t frame_count, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t display_width, uint32_t display_height,
    bool has_sar, uint32_t sar_width, uint32_t sar_height) {
  // First handle the special case of EndOfStream marker showing up at the
  // output.
  if (display_width == kEndOfStreamWidth &&
      display_height == kEndOfStreamHeight) {
    bool is_output_end_of_stream = false;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      if (is_input_end_of_stream_queued_) {
        is_output_end_of_stream = true;
      }
    }  // ~lock
    if (is_output_end_of_stream) {
      decoder_->SetPausedAtEndOfStream();
      video_->TryToRescheduleAssumeVideoDecoderLocked();
      events_->onCoreCodecOutputEndOfStream(false);
      return ZX_ERR_STOP;
    }
  }

  // This is called on a core codec thread, ordered with respect to emitted
  // output frames.  This method needs to block until either:
  //   * Format details have been delivered to the Codec client and the Codec
  //     client has configured corresponding output buffers.
  //   * The client has moved on by closing the current stream, in which case
  //     this method needs to fail quickly so the core codec can be stopped.
  //
  // The video_decoder_lock_ is held during this method.  We don't release the
  // video_decoder_lock_ while waiting for the client, because we want close of
  // the current stream to wait for this method to return before starting the
  // portion of stream close protected by video_decoder_lock_.
  //
  // The signalling to un-block this thread uses lock_.
  //
  // TODO(dustingreen): It can happen that the current set of buffers is already
  // suitable for use under the new buffer constraints.  However, some of the
  // buffers can still be populated with data and used by other parts of the
  // system, so to re-use buffers, we'll need a way to communicate which buffers
  // are not presently available to decode into, even for what vp9_decoder.cc
  // sees as a totally new set of buffers.  The vp9_decoder.cc doesn't separate
  // configuration of a buffer from marking that buffer ready to fill.  For now,
  // we always re-allocate buffers.  Old buffers still active elsewhere in the
  // system can continue to be referenced by those parts of the system - the
  // importan thing for now is we avoid overwriting the content of those buffers
  // by using an entirely new set of buffers for each stream for now.

  // First stash some format and buffer count info needed to initialize frames
  // before triggering mid-stream format change.  Later, frames satisfying these
  // stashed parameters will be handed to the decoder via InitializedFrames(),
  // unless CoreCodecStopStream() happens first.
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);

    // For the moment, force this exact number of frames.
    //
    // TODO(dustingreen): plumb actual frame counts.
    packet_count_total_ = frame_count;
    width_ = width;
    height_ = height;
    stride_ = stride;
    display_width_ = display_width;
    display_height_ = display_height;
    has_sar_ = has_sar;
    sar_width_ = sar_width;
    sar_height_ = sar_height;
  }  // ~lock

  // This will snap the current stream_lifetime_ordinal_, and call
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and
  // CoreCodecMidStreamOutputBufferReConfigFinish() from the StreamControl
  // thread, _iff_ the client hasn't already moved on to a new stream by then.
  events_->onCoreCodecMidStreamOutputConstraintsChange(true);

  return ZX_OK;
}

void CodecAdapterVp9::OnCoreCodecFailStream() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  events_->onCoreCodecFailStream();
}

CodecPacket* CodecAdapterVp9::GetFreePacket() {
  std::lock_guard<std::mutex> lock(lock_);
  uint32_t free_index = free_output_packets_.back();
  free_output_packets_.pop_back();
  return all_output_packets_[free_index];
}
