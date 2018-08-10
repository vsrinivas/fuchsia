// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_h264.h"

#include "device_ctx.h"
#include "h264_decoder.h"
#include "pts_manager.h"
#include "vdec1.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fxl/logging.h>
#include <lib/zx/bti.h>

// TODO(dustingreen):
//   * Split InitializeStream() into two parts, one to get the format info from
//     the HW and send it to the Codec client, the other part to configure
//     output buffers once the client has configured Codec output config based
//     on the format info.  Wire up so that
//     onCoreCodecMidStreamOutputConfigChange() gets called and so that
//     CoreCodecBuildNewOutputConfig() will pick up the correct current format
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
//   * Detect when there's sufficient space in the ring buffer, and feed in
//     partial input packets to permit large input packets with many AUs in
//     them.
//   * At least when promise_separate_access_units_on_input is set, propagate
//     timstamp_ish values from input AU to correct output video frame (using
//     PtsManager).
//   * Consider if there's a way to get AmlogicVideo to re-use buffers across
//     a stream switch without over-writing buffers that are still in-use
//     downstream.

namespace {

// avconv -f lavfi -i color=c=black:s=42x52 -c:v libx264 -profile:v baseline
// -vframes 1 new_stream.h264
//
// (The "baseline" part of the above isn't really needed, but neither is a
// higher profile really needed for this purpose.)
//
// bless new_stream.h264, and manually delete the big SEI NAL that has lots of
// text in it (the exact encoder settings don't really matter for this purpose),
// including its start code, up to just before the next start code, save.
//
// xxd -i new_stream.h264
//
// We push this through the decoder as our "EndOfStream" marker, and detect it
// at the output (for now) by its unusual 42x52 resolution during
// InitializeStream() _and_ the fact that we've queued this marker.  To force
// this frame to be handled by the decoder we queue kFlushThroughBytes of 0
// after this data.
//
// TODO(dustingreen): We don't currently detect the EndOfStream via its stream
// offset in PtsManager (for h264), but that would be marginally more robust
// than detecting the special resolution.  However, to detect via stream offset,
// we'd either need to avoid switching resolutions, or switch resolutions using
// the same output buffer set (including preserving the free/busy status of each
// buffer across the boundary), and delay notifying the client until we're sure
// a format change is real, not just the one immediately before a frame whose
// stream offset is >= the EndOfStream offset.
unsigned char new_stream_h264[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xd9, 0x0c, 0x9e, 0x49,
    0xf0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32,
    0x0f, 0x12, 0x26, 0x48, 0x00, 0x00, 0x00, 0x01, 0x68, 0xcb, 0x83, 0xcb,
    0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x0a, 0xf2, 0x62, 0x80, 0x00,
    0xa7, 0xbc, 0x9c, 0x9d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x78};
unsigned int new_stream_h264_len = 59;

constexpr uint32_t kFlushThroughBytes = 1024;

constexpr uint32_t kEndOfStreamWidth = 42;
constexpr uint32_t kEndOfStreamHeight = 52;

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

}  // namespace

CodecAdapterH264::CodecAdapterH264(std::mutex& lock,
                                   CodecAdapterEvents* codec_adapter_events,
                                   DeviceCtx* device)
    : CodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  FXL_DCHECK(device_);
  FXL_DCHECK(video_);
}

CodecAdapterH264::~CodecAdapterH264() {
  // TODO(dustingreen): Remove the printfs or switch them to VLOG.
  input_processing_loop_.Quit();
  input_processing_loop_.JoinThreads();
  input_processing_loop_.Shutdown();

  // nothing else to do here, at least not until we aren't calling PowerOff() in
  // CoreCodecStopStream().
}

bool CodecAdapterH264::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

void CodecAdapterH264::CoreCodecInit(
    const fuchsia::mediacodec::CodecFormatDetails&
        initial_input_format_details) {
  zx_status_t result = input_processing_loop_.StartThread(
      "CodecAdapterH264::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);

  // TODO(dustingreen): We do most of the setup in CoreCodecStartStream()
  // currently, but we should do more here and less there.
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterH264::CoreCodecStartStream() {
  zx_status_t status;

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    video_->pts_manager_ = std::make_unique<PtsManager>();
    parsed_video_size_ = 0;
    is_input_end_of_stream_queued_ = false;
    video_->core_ = std::make_unique<Vdec1>(video_);
    video_->core()->PowerOn();
    status = video_->InitializeStreamBuffer(true, PAGE_SIZE);
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("InitializeStreamBuffer() failed");
      return;
    }
  }  // ~lock

  {
    std::lock_guard<std::mutex> lock(video_->video_decoder_lock_);
    video_->video_decoder_ = std::make_unique<H264Decoder>(video_);
    status = video_->video_decoder_->Initialize();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec(
          "video_->video_decoder_->Initialize() failed");
      return;
    }

    video_->video_decoder_->SetFrameReadyNotifier(
        [this](std::shared_ptr<VideoFrame> frame) {
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
          io_buffer_cache_flush_invalidate(&frame->buffer,
                                           frame->uv_plane_offset,
                                           frame->stride * frame->height / 2);

          CodecPacket* packet = frame->codec_packet;
          FXL_DCHECK(packet);

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
    video_->video_decoder_->SetInitializeFramesHandler(
        fit::bind_member(this, &CodecAdapterH264::InitializeFramesHandler));
    video_->video_decoder_->SetErrorHandler(
        [this] { events_->onCoreCodecFailStream(); });
  }

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    status = video_->InitializeEsParser();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("InitializeEsParser() failed");
      return;
    }
  }  // ~lock
}

void CodecAdapterH264::CoreCodecQueueInputFormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails&
        per_stream_override_format_details) {
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the CodecFormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterH264::CoreCodecQueueInputPacket(const CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterH264::CoreCodecQueueInputEndOfStream() {
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
void CodecAdapterH264::CoreCodecStopStream() {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This allows InitializeFramesHandler() to essentially cancel and return.
    // The InitializeFramesHandler() is like output and is ordered with respect
    // to output packets, and CoreCodecStopStream() stops both output and
    // InitializeFramesHandler().
    is_stopping_ = true;
    wake_initialize_frames_handler_.notify_all();

    // This helps any previously-queued ProcessInput() calls return faster.
    is_cancelling_input_processing_ = true;
    std::condition_variable stop_input_processing_condition;
    // We know there won't be any new queuing of input, so once this posted work
    // runs, we know all previously-queued ProcessInput() calls have returned.
    PostToInputProcessingThread([this, &stop_input_processing_condition] {
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        FXL_DCHECK(is_cancelling_input_processing_);
        input_queue_.clear();
        is_cancelling_input_processing_ = false;
      }  // ~lock
      stop_input_processing_condition.notify_all();
    });
    while (is_cancelling_input_processing_) {
      stop_input_processing_condition.wait(lock);
    }
    FXL_DCHECK(!is_cancelling_input_processing_);
  }  // ~lock

  // Stop processing queued frames.
  if (video_->core()) {
    video_->core()->StopDecoding();
    video_->core()->WaitForIdle();
  }

  // TODO(dustingreen): Currently, we have to tear down a few pieces of video_,
  // to make it possible to run all the AmlogicVideo + DecoderCore +
  // VideoDecoder code that seems necessary to run to ensure that a new stream
  // will be entirely separate from an old stream, without deleting/creating
  // AmlogicVideo itself.  Probably we can tackle this layer-by-layer, fixing up
  // AmlogicVideo to be more re-usable without the stuff in this method, then
  // DecoderCore, then VideoDecoder.

  {  // scope lock
    std::lock_guard<std::mutex> lock(video_->video_decoder_lock_);
    video_->video_decoder_.reset();
  }  // ~lock

  if (video_->core_) {
    video_->core_->PowerOff();
    video_->core_.reset();
  }

  // The lifetime of this buffer is different than the others in video_, so we
  // have to release it here to avoid leaking when we re-init in
  // CoreCodecStartStream(), for now.
  video_->stream_buffer_.reset();

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    // InitializeFramesHandler() has returned by this point and won't run again
    // until there's a new stream.
    is_stopping_ = false;
  }  // ~lock
}

void CodecAdapterH264::CoreCodecAddBuffer(CodecPort port,
                                          const CodecBuffer* buffer) {
  all_output_buffers_.push_back(buffer);
}

void CodecAdapterH264::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port == kOutputPort) {
    FXL_DCHECK(all_output_packets_.empty());
    FXL_DCHECK(!all_output_buffers_.empty());
    FXL_DCHECK(all_output_buffers_.size() == packets.size());
    for (auto& packet : packets) {
      all_output_packets_.push_back(packet.get());
    }
  }
}

void CodecAdapterH264::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  FXL_DCHECK(!packet->is_new());

  std::shared_ptr<VideoFrame> frame = packet->video_frame().lock();
  if (!frame) {
    // EndOfStream seen at the output, or a new InitializeFrames(), can cause
    // !frame, which is fine.  In that case, any new stream will request
    // allocation of new frames.
    return;
  }

  {  // scope lock
    std::lock_guard<std::mutex> lock(video_->video_decoder_lock_);
    video_->video_decoder_->ReturnFrame(frame);
  }  // ~lock
}

void CodecAdapterH264::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  std::lock_guard<std::mutex> lock(lock_);

  // This adapter should ensure that zero old CodecPacket* or CodecBuffer*
  // remain in this adapter (or below).

  if (port == kInputPort) {
    // There shouldn't be any queued input at this point, but if there is any,
    // fail here even in a release build.
    FXL_CHECK(input_queue_.empty());
  } else {
    FXL_DCHECK(port == kOutputPort);

    // The old all_output_buffers_ are no longer valid.
    all_output_buffers_.clear();
    all_output_packets_.clear();
  }
}

std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
CodecAdapterH264::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  // bear.h264 decodes into 320x192 YUV buffers, but the video display
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

  // For the moment, this codec splits 24 into 22 for the codec and 2 for the
  // client.
  //
  // TODO(dustingreen): Plumb actual frame counts.
  constexpr uint32_t kPacketCountForClientForced = 2;
  // Fairly arbitrary.  The client should set a higher value if the client needs
  // to camp on more frames than this.
  constexpr uint32_t kDefaultPacketCountForClient = kPacketCountForClientForced;

  uint32_t per_packet_buffer_bytes = width_ * height_ * 3 / 2;

  std::unique_ptr<fuchsia::mediacodec::CodecOutputConfig> config =
      std::make_unique<fuchsia::mediacodec::CodecOutputConfig>();

  config->stream_lifetime_ordinal = stream_lifetime_ordinal;
  // For the moment, there will be only one CodecOutputConfig, and it'll need
  // output buffers configured for it.
  FXL_DCHECK(buffer_constraints_action_required);
  config->buffer_constraints_action_required =
      buffer_constraints_action_required;
  config->buffer_constraints.buffer_constraints_version_ordinal =
      new_output_buffer_constraints_version_ordinal;

  // 0 is intentionally invalid - the client must fill out this field.
  config->buffer_constraints.default_settings.buffer_lifetime_ordinal = 0;
  config->buffer_constraints.default_settings
      .buffer_constraints_version_ordinal =
      new_output_buffer_constraints_version_ordinal;
  config->buffer_constraints.default_settings.packet_count_for_codec =
      packet_count_total_ - kPacketCountForClientForced;
  config->buffer_constraints.default_settings.packet_count_for_client =
      kDefaultPacketCountForClient;
  // Packed NV12 (no extra padding, min UV offset, min stride).
  config->buffer_constraints.default_settings.per_packet_buffer_bytes =
      per_packet_buffer_bytes;
  config->buffer_constraints.default_settings.single_buffer_mode = false;

  // For the moment, let's just force the client to allocate this exact size.
  config->buffer_constraints.per_packet_buffer_bytes_min =
      per_packet_buffer_bytes;
  config->buffer_constraints.per_packet_buffer_bytes_recommended =
      per_packet_buffer_bytes;
  config->buffer_constraints.per_packet_buffer_bytes_max =
      per_packet_buffer_bytes;

  // For the moment, let's just force the client to set this exact number of
  // frames for the codec.
  config->buffer_constraints.packet_count_for_codec_min =
      packet_count_total_ - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_codec_recommended =
      packet_count_total_ - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_codec_recommended_max =
      packet_count_total_ - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_codec_max =
      packet_count_total_ - kPacketCountForClientForced;

  config->buffer_constraints.packet_count_for_client_min =
      kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_client_max =
      kPacketCountForClientForced;

  // False because it's not required and not encouraged for a video decoder
  // output to allow single buffer mode.
  config->buffer_constraints.single_buffer_mode_allowed = false;

  config->buffer_constraints.is_physically_contiguous_required = true;
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
  config->buffer_constraints.very_temp_kludge_bti_handle =
      std::move(very_temp_kludge_bti);

  config->format_details.format_details_version_ordinal =
      new_output_format_details_version_ordinal;
  config->format_details.mime_type = "video/raw";

  // For the moment, we'll memcpy to NV12 without any extra padding.
  fuchsia::mediacodec::VideoUncompressedFormat video_uncompressed;
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

  // TODO(dustingreen): Switching to FIDL table should make this not be
  // required.
  video_uncompressed.special_formats.set_temp_field_todo_remove(0);

  fuchsia::mediacodec::VideoFormat video_format;
  video_format.set_uncompressed(std::move(video_uncompressed));

  config->format_details.domain =
      std::make_unique<fuchsia::mediacodec::DomainFormat>();
  config->format_details.domain->set_video(std::move(video_format));

  return config;
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, the core codec just needs us to get new frame buffers
  // set up (while the core codec's interrupt thread sits in
  // InitializeFramesHandler(), so nothing to do here.
  //
  // CoreCodecEnsureBuffersNotConfigured() will run soon.
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Now that the client has configured output buffers, we need to hand those
  // back to the core codec via return of InitializeFramesHandler() which is
  // presently running on the core codec's interrupt thread.
  //
  // We'll let InitializeFramesHandler() deal with converting
  // all_output_buffers_ into a suitable form for return.  Here we just need to
  // wake InitializeFramesHandler().
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_mid_stream_output_config_change_done_ = true;
  }
  wake_initialize_frames_handler_.notify_all();

  // This thread (StreamControl thread) can return immediately here.  Because
  // InitializeFramesHandler() runs with video_decoder_lock_ held the entire
  // time, any further stream switches or similar will be forced to wait for the
  // core codec's interrupt thread to be done processing return of
  // InitializeFramesHandler() before ripping down the frames configured via
  // that return.
}

void CodecAdapterH264::PostSerial(async_dispatcher_t* dispatcher,
                                  fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  FXL_CHECK(post_result == ZX_OK)
      << "async::PostTask() failed - result: " << post_result;
}

void CodecAdapterH264::PostToInputProcessingThread(fit::closure to_run) {
  PostSerial(input_processing_loop_.dispatcher(), std::move(to_run));
}

void CodecAdapterH264::QueueInputItem(CodecInputItem input_item) {
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
        fit::bind_member(this, &CodecAdapterH264::ProcessInput));
  }
}

CodecInputItem CodecAdapterH264::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_cancelling_input_processing_ || input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

void CodecAdapterH264::ProcessInput() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_process_input_queued_ = false;
  }  // ~lock
  while (true) {
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      return;
    }

    if (item.is_format_details()) {
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      FXL_CHECK(item.format_details() == initial_input_format_details_);
      continue;
    }

    if (item.is_end_of_stream()) {
      video_->pts_manager_->SetEndOfStreamOffset(parsed_video_size_);
      video_->ParseVideo(reinterpret_cast<void*>(&new_stream_h264[0]),
                         new_stream_h264_len);
      auto bytes = std::make_unique<uint8_t[]>(kFlushThroughBytes);
      memset(bytes.get(), 0, kFlushThroughBytes);
      video_->ParseVideo(reinterpret_cast<void*>(bytes.get()),
                         kFlushThroughBytes);
      continue;
    }

    FXL_DCHECK(item.is_packet());

    uint8_t* data =
        item.packet()->buffer().buffer_base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

    if (item.packet()->has_timestamp_ish()) {
      video_->pts_manager_->InsertPts(parsed_video_size_,
                                      item.packet()->timestamp_ish());
    }
    parsed_video_size_ += len;

    // This call is the main reason the current thread exists, as this call can
    // wait synchronously until there are empty output frames available to
    // decode into, which can require the shared_fidl_thread() to get those free
    // frames to the Codec server.
    //
    // TODO(dustingreen): This call could be split into a start and complete.
    //
    // TODO(dustingreen): The current wait duration within ParseVideo() assumes
    // that free output frames will become free on an ongoing basis, which isn't
    // really what'll happen when video output is paused.
    video_->ParseVideo(data, len);

    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
  }
}

zx_status_t CodecAdapterH264::InitializeFramesHandler(
    ::zx::bti bti, uint32_t frame_count, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t display_width, uint32_t display_height,
    std::vector<CodecFrame>* frames_out) {
  FXL_DCHECK(frames_out->empty());

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
  // are not presently available to decode into, even for what h264_decoder.cc
  // sees as a totally new set of buffers.  The h264_decoder.cc doesn't seem to
  // separate configuration of a buffer from marking that buffer ready to fill.
  // It seems like "new" buffers are immediately ready to fill.  At the moment,
  // the AmlogicVideo code doesn't appear to show any way to tell the HW which
  // frames are presently still in use (not yet available to decode into),
  // during InitializeStream().  Maybe delaying configuring of a canvas would
  // work, but in that case would the delayed configuring adversely impact
  // decoding performance consistency?  If we can do this, detect when we can,
  // and call onCoreCodecMidStreamOutputConfigChange() but pass false instead of
  // true, and don't expect a response or block in here.  Still have to return
  // the vector of buffers, and will need to indicate which are actually
  // available to decode into.  The rest will get indicated via
  // CoreCodecRecycleOutputPacket(), despite not necessarily getting signalled
  // to the HW by H264Decoder::ReturnFrame further down.  For now, we always
  // re-allocate buffers.  Old buffers still active elsewhere in the system can
  // continue to be referenced by those parts of the system - the importan thing
  // for now is we avoid overwriting the content of those buffers by using an
  // entirely new set of buffers for each stream for now.

  // First, mark that we're processing a mid-stream output config change.  This
  // will get set to false in only two ways:
  //   * CoreCodecStopStream(), in which case the mid-stream output config
  //     change is essentially cancelled.
  //   * In this method, in which case the mid-stream output config change
  //     is finishing with success.
  // All failures will fall under the first bullet, but not all instances of the
  // first bullet are failures, as the client is allowed to move on to a new
  // stream if the client wants to.
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_mid_stream_output_config_change_done_ = false;

    // For the moment, force this exact number of frames.
    //
    // TODO(dustingreen): plumb actual frame counts.
    packet_count_total_ = frame_count;
    width_ = width;
    height_ = height;
    stride_ = stride;
    display_width_ = display_width;
    display_height_ = display_height;
  }  // ~lock

  // This will snap the current stream_lifetime_ordinal_, and call
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and
  // CoreCodecMidStreamOutputBufferReConfigFinish() from the StreamControl
  // thread, _iff_ the client hasn't already moved on to a new stream by then.
  events_->onCoreCodecMidStreamOutputConfigChange(true);

  // The current thread still needs to block until either of the two conditions
  // listed above are true.  The detection strategy for each follows:
  //   * if !is_processing_mid_stream_output_config_change_ already, that means
  //     CoreCodecStopStream() has at least started, which means the config
  //     change is cancelled and this method should return no frames.
  //   * if is_processing_mid_stream_output_config_change_ and
  //     is_done_processing_mid_stream_output_config_change_, that means
  //     while the lock remains held here, CoreCodecStopStream()'s first lock
  //     hold interval has not yet started, and it's safe to build and return
  //     the vector of frames to the caller here.  Even if CoreCodecStopStream()
  //     happens immediately afterward, the point at which CoreCodecStopStream()
  //     acquires video_decoder_lock_ will force CoreCodecStopStream() to wait
  //     until this thread has dealt with frames_out from this method.  In
  //     addition, the CodecPacket*(s) returned remain valid until after
  //     CoreCodecStopStream() has returned - only then is
  //     CoreCodecEnsureBuffersNotConfigured(kOutputPort) called.

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    while (!is_stopping_ && !is_mid_stream_output_config_change_done_) {
      wake_initialize_frames_handler_.wait(lock);
    }

    if (is_stopping_) {
      // CoreCodecStopStream() is essentially cancelling the mid-stream config
      // change.  Return an empty vector.  We need to return so the
      // video_decoder_lock_ can be acquired by CoreCodecStopStream().
      FXL_DCHECK(frames_out->empty());
      return ZX_ERR_CANCELED;
    }

    // Well, it's mostly done.  The remaining portion is to convert the
    // configured buffers into the form needed by frames_out.
    FXL_DCHECK(is_mid_stream_output_config_change_done_);

    // At least for now, we don't implement single_buffer_mode on output of a
    // video decoder, so every frame will have a buffer.
    FXL_DCHECK(all_output_buffers_.size() == packet_count_total_);

    // Now we need to populate the frames_out vector.
    for (uint32_t i = 0; i < frame_count; i++) {
      FXL_DCHECK(all_output_buffers_[i]->buffer_index() == i);
      FXL_DCHECK(all_output_buffers_[i]->codec_buffer().buffer_index == i);
      frames_out->emplace_back(CodecFrame{
          .codec_buffer = fidl::Clone(all_output_buffers_[i]->codec_buffer()),
          .codec_packet = all_output_packets_[i],
      });
    }
  }  // ~lock

  return ZX_OK;
}
