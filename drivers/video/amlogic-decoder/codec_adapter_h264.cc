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
//
// The initial version of this adapter is the minimum required to get any
// decoding to happen at all, and should be read as a very early draft.  This
// version avoids making substantial modifications to layers below for the
// moment.
//
//   * Overall, eliminate copying at the output.
//   * Split InitializeStream() into two parts, one to get the format info from
//     the HW and send it to the Codec client, the other part to configure
//     output buffers once the client has configured Codec output config based
//     on the format info.  Wire up so that
//     onCoreCodecMidStreamOutputConfigChange() gets called and so that
//     CoreCodecBuildNewOutputConfig() will pick up the correct current format
//     info (whether still mid-stream, or at the start of a new stream that's
//     starting before the mid-stream format change was processed for the old
//     stream).
//   * On output side, bidirectional association between VideoFrame and
//     CodecPacket, with underlying memory being the same memory for both
//     representations (it's fine to still have separate "VideoFrame" and
//     "CodecPacket" parts of the overall representation to separate generic
//     concerns from HW-specific concerns, but the underlying memory should be
//     the same memory).
//   * A free output CodecPacket should have its CodecBuffer memory with the
//     HW.
//   * Let the HW's output stride propagate downstream as-is, to permit the
//     output buffers being used as decoder reference frames concurrently with
//     output of same frames.
//   * Allocate output video buffers contig, probably a bool in
//     OnOutputConfig().  Later, set any relevant buffer constraints to indicate
//     contig to BufferAllocator / BufferCollection.
//   * Remove output_processing_thread_ when output copying is no longer a
//     thing.
//   * On EndOfStream at input, push all remaining data through the HW decoder
//     and detect when the EndOfStream is appropriate to generate at the output.
//   * Split video_->Parse() into start/complete and/or switch to feeding the
//     ring buffer directly.
//   * Detect when there's sufficient space in the ring buffer, and feed in
//     partial input packets to permit large input packets with many AUs in
//     them.
//   * At least when promise_separate_access_units_on_input is set, propagate
//     timstamp_ish values from input AU to correct output video frame (using
//     PtsManager).

namespace {

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
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread),
      output_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  FXL_DCHECK(device_);
  FXL_DCHECK(video_);
}

CodecAdapterH264::~CodecAdapterH264() {
  // TODO(dustingreen): Remove the printfs or switch them to VLOG.
  printf("~CodecAdapterH264() stopping input_processing_loop_...\n");
  input_processing_loop_.Quit();
  input_processing_loop_.JoinThreads();
  input_processing_loop_.Shutdown();
  printf("~CodecAdapterH264() done stopping input_processing_loop_.\n");

  // TODO(dustingreen): Remove the printfs or switch them to VLOG.
  printf("~CodecAdapterH264() stopping output_processing_loop_...\n");
  output_processing_loop_.Quit();
  output_processing_loop_.JoinThreads();
  output_processing_loop_.Shutdown();
  printf("~CodecAdapterH264() done stopping output_processing_loop_.\n");

  // nothing else to do here, at least not until we aren't calling PowerOff() in
  // CoreCodecStopStream().
}

bool CodecAdapterH264::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  // bear.h264 is 320x192
  //
  // For the moment, we hotwire those dimensions and require output buffers to
  // be set up in advance of starting decode.

  return true;
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

  result = output_processing_loop_.StartThread(
      "CodecAdapterH264::output_processing_thread_",
      &output_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (output)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);

  // TODO(dustingreen): We do most of the setup in CoreCodecStartStream()
  // currently, but we should do more here and less there.
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterH264::CoreCodecStartStream(
    std::unique_lock<std::mutex>& lock) {
  video_->pts_manager_ = std::make_unique<PtsManager>();
  video_->core_ = std::make_unique<Vdec1>(video_);
  video_->core()->PowerOn();
  zx_status_t status = video_->InitializeStreamBuffer(true, PAGE_SIZE);
  if (status != ZX_OK) {
    events_->onCoreCodecFailCodec("InitializeStreamBuffer() failed");
    return;
  }

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
          printf("Got frame - width: %d height: %d\n", frame->width,
                 frame->height);
          FXL_LOG(INFO) << "Got frame - width: " << frame->width
                        << " height: " << frame->height;

          // TODO(dustingreen): Avoid posting/copying. Bidirectional association
          // between VideoFrame and output CodecPacket.  Get CodecPacket from
          // VideoFrame here, and emit from here.

          bool is_trigger_needed = false;
          {  // scope lock
            std::lock_guard<std::mutex> lock(lock_);
            if (!is_process_output_queued_) {
              is_trigger_needed =
                  ready_output_frames_.empty() && !free_output_packets_.empty();
              is_process_output_queued_ = is_trigger_needed;
            }
            ready_output_frames_.push_back(frame);
          }  // ~lock
          if (is_trigger_needed) {
            PostToOutputProcessingThread(
                fit::bind_member(this, &CodecAdapterH264::ProcessOutput));
          }
        });
    video_->video_decoder_->SetErrorHandler(
        [this] { events_->onCoreCodecFailStream(); });
  }
  status = video_->InitializeEsParser();
  if (status != ZX_OK) {
    events_->onCoreCodecFailCodec("InitializeEsParser() failed");
    return;
  }
}

void CodecAdapterH264::CoreCodecQueueInputFormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails&
        per_stream_override_format_details) {
  printf("CodecAdapterH264::CoreCodecQueueInputFormatDetails() start\n");
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the CodecFormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
  printf("CodecAdapterH264::CoreCodecQueueInputFormatDetails() end\n");
}

void CodecAdapterH264::CoreCodecQueueInputPacket(const CodecPacket* packet) {
  printf("CodecAdapterH264::CoreCodecQueueInputPacket() start\n");
  QueueInputItem(CodecInputItem::Packet(packet));
  printf("CodecAdapterH264::CoreCodecQueueInputPacket() end\n");
}

void CodecAdapterH264::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the HW to necessarily decode all
  // the way up to the marker, depending on whether the client closes the stream
  // or switches to a different stream first - in those cases it's fine for the
  // marker to never show up as output EndOfStream.

  QueueInputItem(CodecInputItem::EndOfStream());
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
void CodecAdapterH264::CoreCodecStopStream(std::unique_lock<std::mutex>& lock) {
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

  // Stop processing queued frames.
  if (video_->core()) {
    video_->core()->StopDecoding();
    video_->core()->WaitForIdle();
  }

  is_cancelling_output_processing_ = true;
  std::condition_variable stop_output_processing_condition;
  PostToOutputProcessingThread([this, &stop_output_processing_condition] {
    std::list<std::shared_ptr<VideoFrame>> to_return;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      FXL_DCHECK(is_cancelling_output_processing_);
      to_return.swap(ready_output_frames_);
      FXL_DCHECK(ready_output_frames_.empty());
      // We intentionally don't mess with free_output_packets_.  Those remain
      // free and can be used for the next stream's output.
    }  // ~lock
    // By returning VideoFrame(s) instead of deleting them, we can use them for
    // a new stream.
    //
    // TODO(dustingreen): Stop deleting all the VideoFrame(s) a bit further
    // down.
    {  // scope lock
      std::lock_guard<std::mutex> lock(video_->video_decoder_lock_);
      for (auto& frame : to_return) {
        // This won't result in more output frames being emitted, because the HW
        // was stopped above.
        video_->video_decoder_->ReturnFrame(frame);
      }
    }  // ~lock
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      is_cancelling_output_processing_ = false;
    }  // ~lock
    stop_output_processing_condition.notify_all();
  });
  while (is_cancelling_output_processing_) {
    stop_output_processing_condition.wait(lock);
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
    printf("video_->core_->PowerOff()...\n");
    video_->core_->PowerOff();
    video_->core_.reset();
    printf("video_->core_.reset() done\n");
  }

  // The lifetime of this buffer is different than the others in video_, so we
  // have to release it here to avoid leaking when we re-init in
  // CoreCodecStartStream(), for now.
  video_->stream_buffer_.reset();
}

void CodecAdapterH264::CoreCodecAddBuffer(CodecPort port,
                                          const CodecBuffer* buffer) {
  // no per-buffer configuration here yet - maybe later
}

void CodecAdapterH264::CoreCodecConfigureBuffers(CodecPort port) {
  // no buffer-setup-done work here yet - maybe later
}

void CodecAdapterH264::CoreCodecRecycleOutputPacketLocked(CodecPacket* packet) {
  bool is_trigger_needed = false;
  if (!is_process_output_queued_) {
    is_trigger_needed =
        free_output_packets_.empty() && !ready_output_frames_.empty();
    is_process_output_queued_ = is_trigger_needed;
  }
  free_output_packets_.push_back(packet);
  if (is_trigger_needed) {
    PostToOutputProcessingThread(
        fit::bind_member(this, &CodecAdapterH264::ProcessOutput));
  }
  // video_->video_decoder_->ReturnFrame(frame) happens on
  // output_processing_thread_.
}

void CodecAdapterH264::CoreCodecEnsureBuffersNotConfiguredLocked(
    CodecPort port) {
  // Given lack of per-buffer or per-buffer-set config so far, what this means
  // for this adapter for now is that this adapter should ensure that zero
  // old CodecPacket* or CodecBuffer* remain in this adapter (or below).  This
  // means the old free_output_packets_ are no longer valid.  There shouldn't be
  // any queued input at this point, but if there is any, fail here even in a
  // release build.
  FXL_CHECK(input_queue_.empty());
  // CodecImpl will later CoreCodecRecycleOutputPacketLocked() on each new
  // packet once those exist.
  free_output_packets_.clear();
}

std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
CodecAdapterH264::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  //
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
  // For the moment, we assume those dimensions and require output buffers to
  // be set up in advance of starting decode.
  //
  // We assume NV12 for the moment.
  //
  // We'll memcpy into NV12 with no extra padding, for the momement.
  //

  constexpr uint32_t kWidth = 320;
  constexpr uint32_t kHeight = 192;

  // For the moment, we'll memcpy so this value doesn't need to be real.
  constexpr uint32_t kMaxReferenceFrames = 6;
  // Reference frames, plus one to be decoding into, plus 1 slack.
  constexpr uint32_t kRecommededPacketCountForCodec = kMaxReferenceFrames + 2;
  // Fairly arbitrary.  The client should set a higher value if the client needs
  // to camp on more frames than this.
  constexpr uint32_t kDefaultPacketCountForClient = 2;
  // No particular limit is enforced by this codec, at least for now.
  constexpr uint32_t kPacketCountForClientMax =
      std::numeric_limits<uint32_t>::max();

  uint32_t width = kWidth;
  uint32_t height = kHeight;
  uint32_t per_packet_buffer_bytes = width * height * 3 / 2;

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
      kRecommededPacketCountForCodec;
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
      kRecommededPacketCountForCodec;
  config->buffer_constraints.packet_count_for_codec_recommended =
      kRecommededPacketCountForCodec;
  config->buffer_constraints.packet_count_for_codec_recommended_max =
      kRecommededPacketCountForCodec;
  config->buffer_constraints.packet_count_for_codec_max =
      kRecommededPacketCountForCodec;

  config->buffer_constraints.packet_count_for_client_max =
      kPacketCountForClientMax;

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
  video_uncompressed.primary_width_pixels = width;
  video_uncompressed.primary_height_pixels = height;
  video_uncompressed.secondary_width_pixels = width / 2;
  video_uncompressed.secondary_height_pixels = height / 2;
  // TODO(dustingreen): remove this field from the VideoUncompressedFormat or
  // specify separately for primary / secondary.
  video_uncompressed.planar = true;
  video_uncompressed.swizzled = false;
  video_uncompressed.primary_line_stride_bytes = width;
  video_uncompressed.secondary_line_stride_bytes = width;
  video_uncompressed.primary_start_offset = 0;
  video_uncompressed.secondary_start_offset = width * height;
  video_uncompressed.tertiary_start_offset = width * height + 1;
  video_uncompressed.primary_pixel_stride = width;
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

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigPrepare(
    std::unique_lock<std::mutex>& lock) {
  // For the moment, this won't be called because
  // onCoreCodecMidStreamOutputConfigChange() won't be called.
  //
  // TODO(dustingreen): Implement - see TODO section at top of file.
  FXL_DCHECK(false) << "not yet implemented";
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigFinish(
    std::unique_lock<std::mutex>& lock) {
  // For the moment, this won't be called because
  // onCoreCodecMidStreamOutputConfigChange() won't be called.
  //
  // TODO(dustingreen): Implement - see TODO section at top of file.
  FXL_DCHECK(false) << "not yet implemented";
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

void CodecAdapterH264::PostToOutputProcessingThread(fit::closure to_run) {
  PostSerial(output_processing_loop_.dispatcher(), std::move(to_run));
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
  printf("CodecAdapterH264::QueueInputItem() is_trigger_needed: %d\n",
         is_trigger_needed);
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
    printf("ProcessInput() top of loop\n");
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      printf("ProcessInput(): !item.is_valid() - input_queue_ was empty.\n");
      return;
    }

    if (item.is_format_details()) {
      printf("ProcessInput() item.is_format_details()\n");
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      FXL_CHECK(item.format_details() == initial_input_format_details_);
      continue;
    }

    if (item.is_end_of_stream()) {
      printf("ProcessInput() item.is_end_of_stream()\n");
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);

        // BEGIN TEMPORARY HACK
        //
        // TODO(dustingreen): Tell HW to finish decoding all previously-queued
        // input, and detect when HW is done doing so async.  At the moment this
        // is a timing-based hack that definitely should not be here, but the
        // hack might allow the HW to finish outputting previosly
        // hw-parser-fetched frames, maybe, sometimes.
        zx_status_t result = async::PostDelayedTask(
            input_processing_loop_.dispatcher(),
            [this] {
              // Other than the duration until this runs, nothing stops there
              // being further output from this stream after this, which is just
              // one of the major issues with this temporary hack.
              bool error_detected_before = false;
              events_->onCoreCodecOutputEndOfStream(error_detected_before);
            },
            zx::sec(4));
        FXL_CHECK(result == ZX_OK);
        //
        // END TEMPORARY HACK
      }  // ~lock
      continue;
    }

    FXL_DCHECK(item.is_packet());
    printf("ProcessInput() item.is_packet()\n");

    uint8_t* data =
        item.packet()->buffer().buffer_base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

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
    printf("before video_->ParseVideo()... - len: %u\n", len);
    video_->ParseVideo(data, len);
    printf("after video_->ParseVideo()\n");

    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
  }
}

void CodecAdapterH264::ProcessOutput() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_process_output_queued_ = false;
  }  // ~lock
  FXL_DCHECK(thrd_current() == output_processing_thread_);
  while (true) {
    // first ready frame
    std::shared_ptr<VideoFrame> frame;
    // first free packet
    CodecPacket* packet = nullptr;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      if (is_cancelling_output_processing_) {
        return;
      }
      if (ready_output_frames_.empty()) {
        return;
      }
      if (free_output_packets_.empty()) {
        return;
      }
      frame = ready_output_frames_.front();
      ready_output_frames_.pop_front();
      packet = free_output_packets_.front();
      free_output_packets_.pop_front();
    }

    // Copy outside the lock.  When stopping the stream we wait for this to be
    // done by posting a subsequent item to output_processing_thread_ and
    // waiting for that item to execute.

    // TODO(dustingreen): Don't copy - see TODO section at top of this file.

    uint64_t total_dst_size_needed = frame->stride * frame->height * 3 / 2;
    FXL_CHECK(total_dst_size_needed <= packet->buffer().buffer_size());

    io_buffer_cache_flush_invalidate(&frame->buffer, 0,
                                     frame->stride * frame->height);
    io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                     frame->stride * frame->height / 2);

    uint8_t* to_y = packet->buffer().buffer_base();
    uint8_t* to_uv =
        packet->buffer().buffer_base() + frame->width * frame->height;
    uint8_t* from_y = static_cast<uint8_t*>(io_buffer_virt(&frame->buffer));
    uint8_t* from_uv = from_y + frame->uv_plane_offset;
    for (uint32_t y = 0; y < frame->height; y++) {
      memcpy(to_y + frame->width * y, from_y + frame->stride * y, frame->width);
    }
    for (uint32_t y = 0; y < frame->height / 2; y++) {
      memcpy(to_uv + frame->width * y, from_uv + frame->stride * y,
             frame->width);
    }

    {  // scope lock
      std::lock_guard<std::mutex> lock(video_->video_decoder_lock_);
      video_->video_decoder_->ReturnFrame(frame);
    }  // ~lock

    packet->SetStartOffset(0);
    packet->SetValidLengthBytes(total_dst_size_needed);

    // TODO(dustingreen): See if we can detect and report errors instead of just
    // "false", if the HW supports that.
    events_->onCoreCodecOutputPacket(packet, false, false);
  }
}
