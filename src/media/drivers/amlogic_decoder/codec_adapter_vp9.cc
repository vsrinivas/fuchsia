// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vp9.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/trace/event.h>
#include <lib/zx/bti.h>

#include "amlogic_codec_adapter.h"
#include "device_ctx.h"
#include "hevcdec.h"
#include "pts_manager.h"
#include "vp9_decoder.h"
#include "vp9_utils.h"

// TODO(dustingreen):
//   * Split InitializeStream() into two parts, one to get the format info from
//     the HW and send it to the Codec client, the other part to configure
//     output buffers once the client has configured Codec output config based
//     on the format info.  Wire up so that
//     onCoreCodecMidStreamOutputConstraintsChange() gets called and so that
//     CoreCodecBuildNewOutputConstraints() will pick up the correct current
//     format info (whether still mid-stream, or at the start of a new stream
//     that's starting before the mid-stream format change was processed for the
//     old stream).
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
    0x44, 0x4b, 0x49, 0x46, 0x00, 0x00, 0x20, 0x00, 0x56, 0x50, 0x39, 0x30, 0x2a, 0x00, 0x34,
    0x00, 0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82,
    0x49, 0x83, 0x42, 0x00, 0x02, 0x90, 0x03, 0x36, 0x00, 0x38, 0x24, 0x1c, 0x18, 0x54, 0x00,
    0x00, 0x30, 0x60, 0x00, 0x00, 0x13, 0xbf, 0xff, 0xfd, 0x15, 0x62, 0x00, 0x00, 0x00};
unsigned int new_stream_ivf_len = 74;
constexpr uint32_t kHeaderSkipBytes = 32 + 12;  // Skip IVF headers.
constexpr uint32_t kFlushThroughBytes = 16384;
constexpr uint32_t kEndOfStreamWidth = 42;
constexpr uint32_t kEndOfStreamHeight = 52;

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
//
// TODO(fxbug.dev/13530): Set this to ~8k or so.  For now, we boost the
// per-packet buffer size to avoid sysmem picking the min buffer size.  The VP9
// conformance streams have AUs that are > 512KiB, so boosting this to 2MiB
// until the decoder handles split AUs on input. We need to be able to fit at
// least 3 of these in the 8MB vdec memory.
constexpr uint32_t kInputPerPacketBufferBytesMin = 2 * 1024 * 1024;
// This is an arbitrary cap for now.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

// Zero-initialized, so it shouldn't take up space on-disk.
const uint8_t kFlushThroughZeroes[kFlushThroughBytes] = {};

constexpr bool kHasSar = false;
constexpr uint32_t kSarWidth = 1;
constexpr uint32_t kSarHeight = 1;

constexpr uint32_t kVdecFifoAlign = 8;

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

}  // namespace

CodecAdapterVp9::CodecAdapterVp9(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                                 DeviceCtx* device)
    : AmlogicCodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
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

bool CodecAdapterVp9::IsCoreCodecRequiringOutputConfigForFormatDetection() { return false; }

bool CodecAdapterVp9::IsCoreCodecMappedBufferUseful(CodecPort port) {
  // If buffers are protected, the decoder should/will call secmem TA to re-pack
  // VP9 headers in the input.  Else the decoder will use a CPU mapping to do
  // this repack.
  //
  // TODO(dustingreen): Make the previous paragraph true.  For now we have to
  // re-pack using the CPU on REE side.
  return true;
}

bool CodecAdapterVp9::IsCoreCodecHwBased(CodecPort port) {
  if (port == kOutputPort) {
    // Output is HW based regardless of whether output is secure or not.
    return true;
  }
  ZX_DEBUG_ASSERT(port == kInputPort);
  // Input is HW based only when secure input at least permitted.
  return IsPortSecurePermitted(kInputPort);
}

zx::unowned_bti CodecAdapterVp9::CoreCodecBti() { return zx::unowned_bti(video_->bti()); }

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

void CodecAdapterVp9::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {
  secure_memory_mode_[port] = secure_memory_mode;
  secure_memory_mode_set_[port] = true;
  if (port == kOutputPort) {
    // Check output secure mode (not input), since overall secure vs. not-secure setup is based on
    // output secure memory mode.  In particular, when output is secure, the stream buffer is
    // secure, which means we can't use the CPU to copy into the stream buffer.  Using parser, input
    // can be non-secure or secure.
    //
    // If output non-secure and input secure:
    //   * by design, this doesn't work for vp9 decode
    // If output non-secure and input non-secure:
    //   * CPU copy from BufferCollection buffer to temp buffer, then parser copy from temp buffer
    //     to stream buffer.  We could skip a copy here, but instead choose to get more efficient
    //     test coverage (at least for now) by keeping this more similar to how secure input works.
    // If output secure and input non-secure:
    //   * CPU copy from BufferCollection buffer to tmp then parser copy from tmp to stream buffer.
    // If output secure and input secure:
    //   * Parser copy from BufferCollection buffer to stream buffer.
    //
    // Always use_parser_, for now.  This is for more efficient / consistent test coverage.
    ZX_DEBUG_ASSERT(use_parser_);
    // use_parser_ is already true.  If output secure, it really must be true.
    ZX_DEBUG_ASSERT(use_parser_ || !IsOutputSecure());
  }
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterVp9::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  fuchsia::sysmem::BufferCollectionConstraints result;

  // For now, we didn't report support for single_buffer_mode, and CodecImpl
  // will have failed the codec already by this point if the client tried to
  // use single_buffer_mode.
  //
  // TODO(dustingreen): Support single_buffer_mode on input (only).
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() ||
                  !partial_settings.single_buffer_mode());

  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  // The CodecImpl already checked that these are set and that they're
  // consistent with packet count constraints.
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());

  if (port == kInputPort) {
    // We don't override CoreCodecBuildNewInputConstraints() for now, so pick these up from what was
    // set by default implementation of CoreCodecBuildNewInputConstraints().
    min_buffer_count_[kInputPort] = stream_buffer_constraints.packet_count_for_server_min();
    max_buffer_count_[kInputPort] = stream_buffer_constraints.packet_count_for_server_max();
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
    per_packet_buffer_bytes_min = stride_ * coded_height_ * 3 / 2;
    // At least for now, don't cap the per-packet buffer size for output.  The
    // HW only cares about the portion we set up for output anyway, and the
    // client has no way to force output to occur into portions of the output
    // buffer beyond what's implied by the max supported image dimensions.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;
  // Non-secure input buffers are never read directly by the hardware, so they don't need to be
  // physically contiguous.
  result.buffer_memory_constraints.physically_contiguous_required =
      (port == kOutputPort) || IsPortSecurePermitted(port);
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
    image_constraints.min_coded_width = 2;
    image_constraints.max_coded_width = 4096;
    image_constraints.min_coded_height = 2;
    // This intentionally isn't the _height_ of a 4k frame, it's intentionally
    // the _width_ of a 4k frame assigned to max_coded_height.
    //
    // See max_coded_width_times_coded_height.  We intentionally constrain the
    // max dimension in width or height to the width of a 4k frame.  While the
    // HW might be able to go bigger than that as long as the other dimension is
    // smaller to compensate, we don't really need to enable any larger than
    // 4k's width in either dimension, so we don't.
    image_constraints.max_coded_height = 4096;
    image_constraints.min_bytes_per_row = 2;
    // no hard-coded max stride, at least for now
    image_constraints.max_bytes_per_row = 0xFFFFFFFF;
    image_constraints.max_coded_width_times_coded_height = 4096 * 2176;
    image_constraints.layers = 1;
    // VP9 decoder writes NV12 frames separately from reference frames, so the
    // coded_width and coded_height aren't constrained to be block aligned.
    //
    // The vp9_decoder code will round up the coded_width to use more of the
    // also-rounded-up stride, so that coded_width can be even even if the
    // HW reported an odd width.
    image_constraints.coded_width_divisor = 2;
    // Unclear how we'd deal with odd coded_height, even if we wanted to.
    image_constraints.coded_height_divisor = 2;
    image_constraints.bytes_per_row_divisor = 32;
    // TODO(dustingreen): Since this is a producer that will always produce at
    // offset 0 of a physical page, we don't really care if this field is
    // consistent with any constraints re. what the HW can do.
    image_constraints.start_offset_divisor = 1;
    // Odd display dimensions are permitted, but these don't necessarily imply
    // odd NV12 coded_width or coded_height dimensions - those are constrainted
    // above.
    //
    // FWIW, the webm VP9 conformance streams seem to think that an odd width
    // implies that there _is_ a chroma sample for the right-most Y samples,
    // since that's how we get the MD5 to match for
    // Profile_0_8bit/frm_resize/crowd_run_1280X768_fr30_bd8_frm_resize_l31.
    // FWIW, the HW VP9 decoder can decode and match the conformance MD5 for
    // that stream, despite it's odd width.
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
    //
    // AFAICT so far, this decoder has no way to output a stride other than
    // fbl::round_up(width_, 32u), so we have to care about stride also.
    image_constraints.required_min_coded_width = coded_width_;
    image_constraints.required_max_coded_width = coded_width_;
    image_constraints.required_min_coded_height = coded_height_;
    image_constraints.required_max_coded_height = coded_height_;
    image_constraints.required_min_bytes_per_row = stride_;
    image_constraints.required_max_bytes_per_row = stride_;
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

void CodecAdapterVp9::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  if (port == kOutputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.has_image_format_constraints);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
                    fuchsia::sysmem::PixelFormatType::NV12);
    output_buffer_collection_info_ = fidl::Clone(buffer_collection_info);
  }
  if (IsPortSecurePermitted(port)) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  }
  buffer_settings_[port].emplace(buffer_collection_info.settings);
}

void CodecAdapterVp9::OnFrameReady(std::shared_ptr<VideoFrame> frame) {
  TRACE_DURATION("media", "CodecAdapterVp9::OnFrameReady", "index", frame->index);
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
  io_buffer_cache_flush_invalidate(&frame->buffer, 0, frame->stride * frame->coded_height);
  io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                   frame->stride * frame->coded_height / 2);

  uint64_t total_size_bytes = frame->stride * frame->coded_height * 3 / 2;
  const CodecBuffer* buffer = frame->codec_buffer;
  ZX_DEBUG_ASSERT(buffer);
  ZX_DEBUG_ASSERT(total_size_bytes <= buffer->size());

  CodecPacket* packet = GetFreePacket();
  // We know there will be a free packet thanks to SetCheckOutputReady().
  ZX_DEBUG_ASSERT(packet);

  packet->SetBuffer(buffer);
  packet->SetStartOffset(0);
  packet->SetValidLengthBytes(total_size_bytes);

  if (frame->has_pts) {
    packet->SetTimstampIsh(frame->pts);
  } else {
    packet->ClearTimestampIsh();
  }

  if (frame->coded_width != output_coded_width_ || frame->coded_height != output_coded_height_ ||
      frame->stride != output_stride_ || frame->display_width != output_display_width_ ||
      frame->display_height != output_display_height_) {
    output_coded_width_ = frame->coded_width;
    output_coded_height_ = frame->coded_height;
    output_stride_ = frame->stride;
    output_display_width_ = frame->display_width;
    output_display_height_ = frame->display_height;
    DLOG(
        "output_coded_width_: %u output_coded_height_: %u output_stride_: %u "
        "output_display_width_: %u output_display_height_: %u",
        output_coded_width_, output_coded_height_, output_stride_, output_display_width_,
        output_display_height_);
    events_->onCoreCodecOutputFormatChange();
  }

  DLOG("onCoreCodecOutputPacket()");
  events_->onCoreCodecOutputPacket(packet, false, false);
}

void CodecAdapterVp9::OnError() {
  LOG(ERROR, "CodecAdapterVp9::OnError()");
  OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
}

void CodecAdapterVp9::OnEos() { OnCoreCodecEos(); }

bool CodecAdapterVp9::IsOutputReady() {
  std::lock_guard<std::mutex> lock(lock_);
  // We're ready if output hasn't been configured yet, or if we have free
  // output packets.  This way the decoder can swap in when there's no output
  // config yet, but will stop trying to run when we're out of output packets.
  return all_output_packets_.empty() || !free_output_packets_.empty();
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterVp9::CoreCodecStartStream() {
  zx_status_t status;

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    parsed_video_size_ = 0;
    is_input_end_of_stream_queued_to_core_ = false;
    has_input_keyframe_ = false;
    is_stream_failed_ = false;
    ZX_DEBUG_ASSERT(queued_frame_sizes_.empty());
  }  // ~lock

  auto decoder = std::make_unique<Vp9Decoder>(video_, this, Vp9Decoder::InputType::kMultiStream,
                                              false, IsOutputSecure());
  decoder->SetFrameDataProvider(this);

  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    status = decoder->InitializeBuffers();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("video_->video_decoder_->Initialize() failed");
      return;
    }

    auto instance = std::make_unique<DecoderInstance>(std::move(decoder), video_->hevc_core());
    // The video decoder can read from non-secure buffers even in secure mode.
    status = video_->AllocateStreamBuffer(instance->stream_buffer(), 512 * PAGE_SIZE,
                                          /*use_parser=*/use_parser_,
                                          /*is_secure=*/IsPortSecure(kInputPort));
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
  QueueInputItem(CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterVp9::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterVp9::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the HW to necessarily decode all
  // the way up to the marker, depending on whether the client closes the stream
  // or switches to a different stream first - in those cases it's fine for the
  // marker to never show up as output EndOfStream.

  QueueInputItem(CodecInputItem::EndOfStream());
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
void CodecAdapterVp9::CoreCodecStopStream() {
  std::list<CodecInputItem> leftover_input_items = CoreCodecStopStreamInternal();
  for (auto& input_item : leftover_input_items) {
    if (input_item.is_packet()) {
      events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
    }
  }
}

std::list<CodecInputItem> CodecAdapterVp9::CoreCodecStopStreamInternal() {
  std::list<CodecInputItem> input_items_result;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This helps any previously-queued ProcessInput() calls return faster.
    is_cancelling_input_processing_ = true;
    std::condition_variable stop_input_processing_condition;
    // We know there won't be any new queuing of input, so once this posted work
    // runs, we know all previously-queued ProcessInput() calls have returned.
    PostToInputProcessingThread([this, &stop_input_processing_condition, &input_items_result] {
      std::list<CodecInputItem> leftover_input_items;
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        ZX_DEBUG_ASSERT(is_cancelling_input_processing_);
        ZX_DEBUG_ASSERT(input_items_result.empty());
        input_items_result.swap(input_queue_);
        is_cancelling_input_processing_ = false;
      }  // ~lock
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
    }  // ~lock
    // If the decoder's still running this will stop it as well.
    video_->RemoveDecoder(decoder_to_remove);
  }

  queued_frame_sizes_.clear();

  return input_items_result;
}

void CodecAdapterVp9::CoreCodecResetStreamAfterCurrentFrame() {
  // Currently this takes ~20-40ms per reset.  We might be able to improve the performance by having
  // a stop that doesn't deallocate followed by a start that doesn't allocate, but since we'll
  // fairly soon only be using this method during watchdog processing, it's not worth optimizing for
  // the temporary time interval during which we might potentially use this on multiple
  // non-keyframes in a row before a keyframe, only in the case of protected input.
  //
  // If we were to optimize in that way, it'd increase the complexity of init and de-init code.  The
  // current way we use that code exactly the same way for reset as for init and de-init, which is
  // good from a test coverage point of view.

  // This fences and quiesces the input processing thread, and the current StreamControl thread is
  // the only other thread that modifies is_input_end_of_stream_queued_to_core_, so we know
  // is_input_end_of_stream_queued_to_core_ won't be changing.
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

void CodecAdapterVp9::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
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
    // This should prevent any inadvertent dependence by clients on the ordering
    // of packet_index values in the output stream or any assumptions re. the
    // relationship between packet_index and buffer_index.
    std::shuffle(free_output_packets_.begin(), free_output_packets_.end(), not_for_security_prng_);
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
    output_buffer_collection_info_.reset();
  }
  buffer_settings_[port].reset();
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterVp9::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
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

  // Fairly arbitrary.  The client should set a higher value if the client needs
  // to camp on more frames than this.
  constexpr uint32_t kDefaultPacketCountForClient = 2;

  uint32_t per_packet_buffer_bytes = stride_ * coded_height_ * 3 / 2;

  auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();
  auto* default_settings = constraints->mutable_default_settings();

  // For the moment, there will be only one StreamOutputConstraints, and it'll
  // need output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // 0 is intentionally invalid - the client must fill out this field.
  default_settings->set_buffer_lifetime_ordinal(0);
  default_settings->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);
  default_settings->set_packet_count_for_server(min_buffer_count_[kOutputPort]);
  default_settings->set_packet_count_for_client(kDefaultPacketCountForClient);
  // Packed NV12 (no extra padding, min UV offset, min stride).
  default_settings->set_per_packet_buffer_bytes(per_packet_buffer_bytes);
  default_settings->set_single_buffer_mode(false);

  // For the moment, let's just force the client to allocate this exact size.
  constraints->set_per_packet_buffer_bytes_min(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_recommended(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_max(per_packet_buffer_bytes);

  constraints->set_packet_count_for_server_min(min_buffer_count_[kOutputPort]);
  constraints->set_packet_count_for_server_recommended(min_buffer_count_[kOutputPort]);
  constraints->set_packet_count_for_server_recommended_max(max_buffer_count_[kOutputPort]);
  constraints->set_packet_count_for_server_max(max_buffer_count_[kOutputPort]);

  constraints->set_packet_count_for_client_min(0);
  constraints->set_packet_count_for_client_max(max_buffer_count_[kOutputPort]);

  // False because it's not required and not encouraged for a video decoder
  // output to allow single buffer mode.
  constraints->set_single_buffer_mode_allowed(false);

  constraints->set_is_physically_contiguous_required(true);

  return config;
}

fuchsia::media::StreamOutputFormat CodecAdapterVp9::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()->set_format_details_version_ordinal(
      new_output_format_details_version_ordinal);
  result.mutable_format_details()->set_mime_type("video/raw");

  // For the moment, we'll memcpy to NV12 without any extra padding.
  fuchsia::media::VideoUncompressedFormat video_uncompressed;
  video_uncompressed.fourcc = make_fourcc('N', 'V', '1', '2');
  video_uncompressed.primary_width_pixels = output_coded_width_;
  video_uncompressed.primary_height_pixels = output_coded_height_;
  video_uncompressed.secondary_width_pixels = output_coded_width_ / 2;
  video_uncompressed.secondary_height_pixels = output_coded_height_ / 2;
  // TODO(dustingreen): remove this field from the VideoUncompressedFormat or
  // specify separately for primary / secondary.
  video_uncompressed.planar = true;
  video_uncompressed.swizzled = false;
  video_uncompressed.primary_line_stride_bytes = output_stride_;
  video_uncompressed.secondary_line_stride_bytes = output_stride_;
  video_uncompressed.primary_start_offset = 0;
  video_uncompressed.secondary_start_offset = output_stride_ * output_coded_height_;
  video_uncompressed.tertiary_start_offset = output_stride_ * output_coded_height_ + 1;
  video_uncompressed.primary_pixel_stride = 1;
  video_uncompressed.secondary_pixel_stride = 2;
  video_uncompressed.primary_display_width_pixels = output_display_width_;
  video_uncompressed.primary_display_height_pixels = output_display_height_;
  video_uncompressed.has_pixel_aspect_ratio = kHasSar;
  video_uncompressed.pixel_aspect_ratio_width = kSarWidth;
  video_uncompressed.pixel_aspect_ratio_height = kSarHeight;

  video_uncompressed.image_format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
  video_uncompressed.image_format.coded_width = output_coded_width_;
  video_uncompressed.image_format.coded_height = output_coded_height_;
  video_uncompressed.image_format.bytes_per_row = output_stride_;
  video_uncompressed.image_format.display_width = output_display_width_;
  video_uncompressed.image_format.display_height = output_display_height_;
  video_uncompressed.image_format.layers = 1;
  video_uncompressed.image_format.color_space.type = fuchsia::sysmem::ColorSpaceType::REC709;
  video_uncompressed.image_format.has_pixel_aspect_ratio = kHasSar;
  video_uncompressed.image_format.pixel_aspect_ratio_width = kSarWidth;
  video_uncompressed.image_format.pixel_aspect_ratio_height = kSarHeight;

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(std::move(video_uncompressed));

  result.mutable_format_details()->mutable_domain()->set_video(std::move(video_format));

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
  uint32_t coded_width;
  uint32_t coded_height;
  uint32_t stride;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // Now we need to populate the frames_out vector.
    for (uint32_t i = 0; i < all_output_buffers_.size(); i++) {
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->index() == i);
      frames.emplace_back(*all_output_buffers_[i]);
    }
    coded_width = coded_width_;
    coded_height = coded_height_;
    stride = stride_;
  }  // ~lock
  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    video_->video_decoder()->InitializedFrames(std::move(frames), coded_width, coded_height,
                                               stride);
  }  // ~lock
}

void CodecAdapterVp9::PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  ZX_ASSERT_MSG(post_result == ZX_OK, "async::PostTask() failed - result: %d\n", post_result);
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
    PostToInputProcessingThread(fit::bind_member(this, &CodecAdapterVp9::ProcessInput));
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
    PostToInputProcessingThread(fit::bind_member(this, &CodecAdapterVp9::ProcessInput));
  }
}

bool CodecAdapterVp9::HasMoreInputData() {
  if (queued_frame_sizes_.size() > 0)
    return true;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ || input_queue_.empty()) {
      return false;
    }
  }  // ~lock
  return true;
}

void CodecAdapterVp9::AsyncResetStreamAfterCurrentFrame() {
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

CodecInputItem CodecAdapterVp9::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ || input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

// If paddr_size != 0, paddr_base is used to submit data to the HW directly by physical address.
// Otherwise, vaddr_base and vaddr_size are valid, and are used to submit data to the HW.
void CodecAdapterVp9::SubmitDataToStreamBuffer(zx_paddr_t paddr_base, uint32_t paddr_size,
                                               uint8_t* vaddr_base, uint32_t vaddr_size) {
  ZX_DEBUG_ASSERT(paddr_size == 0 || use_parser_);
  video_->AssertVideoDecoderLockHeld();
  zx_status_t status;
  if (use_parser_) {
    status = video_->SetProtected(VideoDecoder::Owner::ProtectableHardwareUnit::kParser,
                                  IsPortSecure(kInputPort));
    if (status != ZX_OK) {
      LOG(ERROR, "video_->SetProtected(kParser) failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    // Pass nullptr because we'll handle syncing updates manually.
    status = video_->parser()->InitializeEsParser(nullptr);
    if (status != ZX_OK) {
      DECODE_ERROR("InitializeEsParser failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    uint32_t size = paddr_size ? paddr_size : vaddr_size;
    if (size + sizeof(kFlushThroughZeroes) > video_->GetStreamBufferEmptySpace()) {
      // We don't want the parser to hang waiting for output buffer space, since new space will
      // never be released to it since we need to manually update the read pointer. TODO(fxbug.dev/41825):
      // Handle copying only as much as can fit and waiting for kVp9InputBufferEmpty to continue
      // copying the remainder.
      DECODE_ERROR("Empty space in stream buffer %d too small for video data (%lu)",
                   video_->GetStreamBufferEmptySpace(), size + sizeof(kFlushThroughZeroes));
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    video_->parser()->SyncFromDecoderInstance(video_->current_instance());

    if (paddr_size) {
      status = video_->parser()->ParseVideoPhysical(paddr_base, paddr_size);
    } else {
      status = video_->parser()->ParseVideo(vaddr_base, vaddr_size);
    }
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing video failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    status = video_->parser()->WaitForParsingCompleted(ZX_SEC(10));
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing video timed out - status: %d", status);
      video_->parser()->CancelParsing();
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    status = video_->parser()->ParseVideo(kFlushThroughZeroes, sizeof(kFlushThroughZeroes));
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing flush-through zeros failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    status = video_->parser()->WaitForParsingCompleted(ZX_SEC(10));
    if (status != ZX_OK) {
      DECODE_ERROR("Parsing flush-through zeros timed out - status: %d", status);
      video_->parser()->CancelParsing();
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }

    video_->parser()->SyncToDecoderInstance(video_->current_instance());
  } else {
    ZX_DEBUG_ASSERT(!paddr_size);
    status = video_->ProcessVideoNoParser(vaddr_base, vaddr_size);
    if (status != ZX_OK) {
      LOG(ERROR, "video_->ProcessVideoNoParser() (data) failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
    status = video_->ProcessVideoNoParser(kFlushThroughZeroes, sizeof(kFlushThroughZeroes));
    if (status != ZX_OK) {
      LOG(ERROR, "video_->ProcessVideoNoParser() (zeroes) failed - status: %d", status);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }
  }
}

// The decoder lock is held by caller during this method.
void CodecAdapterVp9::ReadMoreInputData(Vp9Decoder* decoder) {
  LOG(DEBUG, "top");
  // Typically we only get one frame from the FW per UpdateDecodeSize(), but if we submitted more
  // than one frame of a superframe to the FW at once, we _sometimes_ get more than one frame from
  // the FW before the kVp9CommandNalDecodeDone (and before the subsequent UpdateDecodeSize() for
  // the next frame that we would have done).
  //
  // By adjusting queued_frame_sizes_ here when we get more than one frame, we avoid asking the FW
  // to keep decoding if it's already delivered all the frames we're expecting.  If we ask the FW to
  // keep decoding after all expected frames, we just end up hitting the watchdog (the FW doesn't
  // indicate kVp9CommandNalDecodeDone unless a frame was emitted subsequent to the previous
  // UpdateDecodeSize(), as far as I can tell).
  //
  // If input data isn't hostile, we likely will never see queued_frame_sizes_.size() == 0 when
  // decoder->FramesSinceUpdateDecodeSize() > 1, but it doesn't hurt to check, in case the FW can
  // somehow be influenced to output more frames than the number of AMLV headers we added.
  if (decoder->FramesSinceUpdateDecodeSize() > 1 && queued_frame_sizes_.size() != 0) {
    // We expect 1, and we already removed that 1 from queued_frame_sizes_ previously.  If more than
    // 1 frame was indicated by the FW, then for each of the extra frames, we need to reduce the
    // size of queued_frame_sizes_ by 1, without changing the sum of the values, unless the last
    // item is being removed in which case the last item is removed and the sum becomes 0.
    for (uint32_t i = 0; i < decoder->FramesSinceUpdateDecodeSize() - 1; ++i) {
      DLOG(
          "decoder->FramesSinceUpdateDecodeSize() > 1 -- "
          "decoder->FramesSinceUpdateDecodeSize(): %u queued_frame_sizes_.front(): %u "
          "queued_frame_sizes_.size(): %lu",
          decoder->FramesSinceUpdateDecodeSize(), queued_frame_sizes_.front(),
          queued_frame_sizes_.size());
      uint32_t old_front_frame_size = queued_frame_sizes_.front();
      queued_frame_sizes_.erase(queued_frame_sizes_.begin());
      if (queued_frame_sizes_.size() == 0) {
        // Done with all the frames we expected to see at the output, so move on to submit new data
        // to the FIFO.  If we instead were to attempt to deliver the last few bytes to the FW, it
        // would just get stuck not interrupting and not indicating that the input data is consumed
        // (dec_status 0xe never happens).
        //
        // Despite skipping past some DecodeSize, the parsed_video_size_ stays in sync; apparently
        // it really is parsed_decode_size_, not decoder_processed_bytes_.
        break;
      }
      // Should still UpdateDecodeSize() with all the data of the superframe, overall, to avoid the
      // possibility of not actually giving the last byte of the last frame to the FW.
      queued_frame_sizes_.front() += old_front_frame_size;
    }
  }

  if (queued_frame_sizes_.size()) {
    DLOG("UpdateDecodeSize() (from prev)");
    decoder->UpdateDecodeSize(queued_frame_sizes_.front());
    queued_frame_sizes_.erase(queued_frame_sizes_.begin());
    return;
  }

  while (true) {
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      LOG(DEBUG, "!item.is_valid()");
      return;
    }

    if (item.is_format_details()) {
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      ZX_ASSERT(fidl::Equals(item.format_details(), initial_input_format_details_));
      continue;
    }

    if (item.is_end_of_stream()) {
      DLOG("SetEndOfStreamOffset() - parsed_video_size_: 0x%lx", parsed_video_size_);
      video_->pts_manager()->SetEndOfStreamOffset(parsed_video_size_);
      std::vector<uint8_t> split_data;
      std::vector<uint32_t> frame_sizes;
      SplitSuperframe(reinterpret_cast<const uint8_t*>(&new_stream_ivf[kHeaderSkipBytes]),
                      new_stream_ivf_len - kHeaderSkipBytes, &split_data, &frame_sizes);
      ZX_DEBUG_ASSERT(frame_sizes.size() == 1);
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        is_input_end_of_stream_queued_to_core_ = true;
      }  // ~lock
      SubmitDataToStreamBuffer(/*paddr_base=*/0, /*paddr_size=*/0, split_data.data(),
                               split_data.size());
      // Intentionally not including kFlushThroughZeroes - this only includes
      // data in AMLV frames.
      DLOG("UpdateDecodeSize() (EOS)");
      decoder->UpdateDecodeSize(split_data.size());
      return;
    }

    ZX_DEBUG_ASSERT(item.is_packet());
    auto return_input_packet =
        fit::defer([this, &item] { events_->onCoreCodecInputPacketDone(item.packet()); });

    uint8_t* data = item.packet()->buffer()->base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

    zx_paddr_t paddr_base = 0;
    uint32_t paddr_size = 0;
    std::vector<uint8_t> split_data;
    // If we're using TeeVp9AddHeaders() we don't actually populate split_data, but we still care
    // what the size would have been.
    uint32_t after_repack_len = 0;
    std::vector<uint32_t> new_queued_frame_sizes;

    if (IsPortSecure(kInputPort)) {
      ZX_DEBUG_ASSERT(item.packet()->buffer()->is_pinned());
      paddr_base = item.packet()->buffer()->physical_base() + item.packet()->start_offset();

      // These are enforced by codec_impl.cc as a packet arrives.
      ZX_DEBUG_ASSERT(len > 0);
      ZX_DEBUG_ASSERT(item.packet()->start_offset() + len <= item.packet()->buffer()->size());

      zx_status_t status = video_->TeeVp9AddHeaders(
          paddr_base, len, item.packet()->buffer()->size() - item.packet()->start_offset(),
          &after_repack_len);
      if (status != ZX_OK) {
        LOG(ERROR, "TeeVp9AddHeaders() failed - status: %d", status);
        OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
        return;
      }

      paddr_size = after_repack_len;

      ZX_DEBUG_ASSERT(new_queued_frame_sizes.size() == 0);
    } else {
      // We split superframes essentially the same way TeeVp9AddHeaders() does, to share as much
      // handling as we can regardless of whether IsPortSecure(kInputPort).
      SplitSuperframe(data, len, &split_data, &new_queued_frame_sizes, /*like_secmem=*/true);
      ZX_DEBUG_ASSERT(!new_queued_frame_sizes.empty());
      after_repack_len = split_data.size();
      // Because like_sysmem true, the after_repack_len includes an extraneous superframe footer
      // size also, just like TeeVp9AddHeaders().
      ZX_DEBUG_ASSERT(after_repack_len == len + new_queued_frame_sizes.size() * kVp9AmlvHeaderSize);
    }

    uint8_t* vaddr_base = nullptr;
    uint32_t vaddr_size = 0;
    if (!paddr_base) {
      vaddr_base = split_data.data();
      vaddr_size = split_data.size();
    }

    // For now, we only have known frame sizes for non-DRM streams.  In future we intend to have
    // known frame sizes regardless of DRM or not.
    ZX_DEBUG_ASSERT(!IsPortSecure(kInputPort) == !new_queued_frame_sizes.empty());
    if (!has_input_keyframe_ && !new_queued_frame_sizes.empty()) {
      // for now
      ZX_DEBUG_ASSERT(vaddr_base && vaddr_size && !paddr_base && !paddr_size);
      while (!new_queued_frame_sizes.empty()) {
        uint8_t* vp9_frame_header = vaddr_base + kVp9AmlvHeaderSize;
        if (vp9_frame_header >= vaddr_base + vaddr_size) {
          LOG(ERROR, "frame_type parsing failed");
          OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_DATA_PARSING);
          return;
        }
        auto is_key_frame_result = IsVp9KeyFrame(*vp9_frame_header);
        if (!is_key_frame_result.is_ok()) {
          OnCoreCodecFailStream(is_key_frame_result.error());
          return;
        }
        bool skip_frame = !is_key_frame_result.value();
        if (skip_frame) {
          // Skip the first frame.
          uint32_t amlv_frame_size = new_queued_frame_sizes.front();
          ZX_DEBUG_ASSERT(vaddr_size >= amlv_frame_size);
          ZX_DEBUG_ASSERT(after_repack_len >= amlv_frame_size);
          vaddr_base += amlv_frame_size;
          vaddr_size -= amlv_frame_size;
          after_repack_len -= amlv_frame_size;
          if (paddr_size) {
            // This will become important later when we have both vaddr_base and paddr_base with
            // valid data, with paddr_base protected and vaddr_base clear.
            ZX_DEBUG_ASSERT(paddr_size >= amlv_frame_size);
            paddr_base += amlv_frame_size;
            paddr_size -= amlv_frame_size;
          }
          new_queued_frame_sizes.erase(new_queued_frame_sizes.begin());
          // next frame of superframe, if any
          continue;
        }
        // We didn't find any reason to skip the (now) first frame which is a keyframe, so note we
        // have a keyframe and break out of "!new_queued_frame_sizes.empty()" loop so it can be
        // submitted to HW along with any subsequent frames of its superframe.
        ZX_DEBUG_ASSERT(!new_queued_frame_sizes.empty());
        has_input_keyframe_ = true;
        break;
      }
      if (new_queued_frame_sizes.empty()) {
        // The vaddr_size can still be non-zero here, due to superframe header bytes, which is fine.
        //
        // next input item, if any
        //
        // ~return_input_packet, ~item
        continue;
      }
    }

    uint32_t increased_size = after_repack_len - len;
    if ((after_repack_len < len) || (increased_size % 16 != 0) || (increased_size < 16)) {
      LOG(ERROR, "Repack gave bad size 0x%x from 0x%x", after_repack_len, len);
      OnCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return;
    }

    //////////////////////////////
    // No failures from here down.
    //////////////////////////////

    LOG(DEBUG, "InsertPts() - parsed_video_size_: 0x%lx has_timestamp_ish: %u timestamp_ish: %lu",
        parsed_video_size_, item.packet()->has_timestamp_ish(), item.packet()->timestamp_ish());
    video_->pts_manager()->InsertPts(parsed_video_size_, item.packet()->has_timestamp_ish(),
                                     item.packet()->timestamp_ish());

    uint32_t frame_count = increased_size / 16;
    LOG(DEBUG, "frame_count: 0x%x protected: %u", frame_count, IsPortSecure(kInputPort));

    // Because TeeVp9AddHeaders() doesn't output the frame sizes within a superframe, we
    // intentionally ignore those, even when the input data is non-protected, to keep the handling
    // of protected and non-protected similar, for efficient test coverage.
    new_queued_frame_sizes.clear();
    new_queued_frame_sizes.push_back(after_repack_len - (frame_count - 1) * kVdecFifoAlign);
    for (uint32_t i = 1; i < frame_count; ++i) {
      new_queued_frame_sizes.push_back(kVdecFifoAlign);
    }

    parsed_video_size_ += after_repack_len + kFlushThroughBytes;
    SubmitDataToStreamBuffer(paddr_base, paddr_size, vaddr_base, vaddr_size);
    queued_frame_sizes_ = std::move(new_queued_frame_sizes);
    ZX_DEBUG_ASSERT(!queued_frame_sizes_.empty());

    LOG(DEBUG, "UpdateDecodeSize() (new)");
    decoder->UpdateDecodeSize(queued_frame_sizes_.front());
    queued_frame_sizes_.erase(queued_frame_sizes_.begin());

    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~return_input_packet, ~item
    return;
  }
}

bool CodecAdapterVp9::IsCurrentOutputBufferCollectionUsable(
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
  if (info.buffer_count > max_frame_count) {
    // The vp9_decoder.cc won't exercise this path since the max is always the same, and we won't
    // have allocated a collection with more than max_buffer_count.
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

zx_status_t CodecAdapterVp9::InitializeFrames(::zx::bti bti, uint32_t min_frame_count,
                                              uint32_t max_frame_count, uint32_t coded_width,
                                              uint32_t coded_height, uint32_t stride,
                                              uint32_t display_width, uint32_t display_height,
                                              bool has_sar, uint32_t sar_width,
                                              uint32_t sar_height) {
  ZX_DEBUG_ASSERT(!has_sar);
  ZX_DEBUG_ASSERT(sar_width == 1);
  ZX_DEBUG_ASSERT(sar_height == 1);
  // First handle the special case of EndOfStream marker showing up at the
  // output.  We want to notice if up to this point we've been decoding into
  // buffers smaller than this.  By noticing here, we avoid requiring the client
  // to re-allocate buffers just before EOS.
  if (display_width == kEndOfStreamWidth && display_height == kEndOfStreamHeight) {
    bool is_output_end_of_stream = false;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      if (is_input_end_of_stream_queued_to_core_) {
        is_output_end_of_stream = true;
      }
    }  // ~lock
    if (is_output_end_of_stream) {
      OnCoreCodecEos();
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
    min_buffer_count_[kOutputPort] = min_frame_count;
    max_buffer_count_[kOutputPort] = max_frame_count;

    coded_width_ = coded_width;
    coded_height_ = coded_height;
    stride_ = stride;
    display_width_ = display_width;
    display_height_ = display_height;
  }  // ~lock

  // This will snap the current stream_lifetime_ordinal_, and call
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and
  // CoreCodecMidStreamOutputBufferReConfigFinish() from the StreamControl
  // thread, _iff_ the client hasn't already moved on to a new stream by then.
  events_->onCoreCodecMidStreamOutputConstraintsChange(true);

  return ZX_OK;
}

void CodecAdapterVp9::OnCoreCodecEos() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT(is_input_end_of_stream_queued_to_core_);
  }  // ~lock
  decoder_->SetPausedAtEndOfStream();
  video_->AssertVideoDecoderLockHeld();
  video_->TryToReschedule();
  events_->onCoreCodecOutputEndOfStream(false);
}

void CodecAdapterVp9::OnCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }  // ~lock
  LOG(ERROR, "CodecAdapterVp9::OnCoreCodecFailStream()");
  events_->onCoreCodecFailStream(error);
}

CodecPacket* CodecAdapterVp9::GetFreePacket() {
  std::lock_guard<std::mutex> lock(lock_);
  uint32_t free_index = free_output_packets_.back();
  free_output_packets_.pop_back();
  return all_output_packets_[free_index];
}

bool CodecAdapterVp9::IsPortSecureRequired(CodecPort port) {
  ZX_DEBUG_ASSERT(secure_memory_mode_set_[port]);
  return secure_memory_mode_[port] == fuchsia::mediacodec::SecureMemoryMode::ON;
}

bool CodecAdapterVp9::IsPortSecurePermitted(CodecPort port) {
  ZX_DEBUG_ASSERT(secure_memory_mode_set_[port]);
  return secure_memory_mode_[port] != fuchsia::mediacodec::SecureMemoryMode::OFF;
}

bool CodecAdapterVp9::IsPortSecure(CodecPort port) {
  ZX_DEBUG_ASSERT(secure_memory_mode_set_[port]);
  ZX_DEBUG_ASSERT(buffer_settings_[port]);
  return buffer_settings_[port]->buffer_settings.is_secure;
}

bool CodecAdapterVp9::IsOutputSecure() {
  // We need to know whether output is secure or not before we start accepting input, which means
  // we need to know before output buffers are allocated, which means we can't rely on the result
  // of sysmem BufferCollection allocation is_secure for output.
  ZX_DEBUG_ASSERT(IsPortSecurePermitted(kOutputPort) == IsPortSecureRequired(kOutputPort));
  return IsPortSecureRequired(kOutputPort);
}
