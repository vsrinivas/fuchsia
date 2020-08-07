// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "use_video_decoder.h"

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/defer.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/media/test/codec_client.h>
#include <lib/media/test/frame_sink.h>
#include <lib/media/test/one_shot_event.h>
#include <lib/syslog/cpp/macros.h>
#include <stdint.h>
#include <string.h>

#include <thread>
#include <vector>

#include <fbl/algorithm.h>
#include <src/media/lib/raw_video_writer/raw_video_writer.h>
#include <tee-client-api/tee-client-types.h>

#include "in_stream_peeker.h"
#include "input_copier.h"
#include "lib/zx/time.h"
#include "util.h"

namespace {

// Most cases secure output can't be read to be verified, but under some testing
// circumstances it can be possible.
constexpr bool kVerifySecureOutput = false;

// Queue SPS and PPS separately from the subsequent picture slice.
constexpr bool kH264SeparateSpsPps = true;

// Force some splitting of frames across packet boundaries.  The remainder of the frame data will go
// in subsequent packets.
constexpr size_t kMaxFrameBytesPerPacket = 4ul * 1024;

constexpr zx::duration kInStreamDeadlineDuration = zx::sec(30);

// This example only has one stream_lifetime_ordinal which is 1.
//
// TODO(dustingreen): actually re-use the Codec instance for at least one more
// stream, even if it's just to decode the same data again.
constexpr uint64_t kStreamLifetimeOrdinal = 1;

// Scenic ImagePipe doesn't allow image_id 0, so offset by this much.
constexpr uint32_t kFirstValidImageId = 1;

constexpr uint8_t kLongStartCodeArray[] = {0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kShortStartCodeArray[] = {0x00, 0x00, 0x01};

// If readable_bytes is 0, that's considered a "start code", to allow the caller
// to terminate a NAL the same way regardless of whether another start code is
// found or the end of the buffer is found.
//
// ptr has readable_bytes of data - the function only evaluates whether there is
// a start code at the beginning of the data at ptr.
//
// readable_bytes - the caller indicates how many bytes are readable starting at
// ptr.
//
// *start_code_size_bytes will have length of the start code in bytes when the
// function returns true - unchanged otherwise.  Normally this would be 3 or 4,
// but a 0 is possible if readable_bytes is 0.
bool is_start_code(uint8_t* ptr, size_t readable_bytes, size_t* start_code_size_bytes_out) {
  if (readable_bytes == 0) {
    *start_code_size_bytes_out = 0;
    return true;
  }
  if (readable_bytes >= 4) {
    if (!memcmp(ptr, kLongStartCodeArray, sizeof(kLongStartCodeArray))) {
      *start_code_size_bytes_out = 4;
      return true;
    }
  }
  if (readable_bytes >= 3) {
    if (!memcmp(ptr, kShortStartCodeArray, sizeof(kShortStartCodeArray))) {
      *start_code_size_bytes_out = 3;
      return true;
    }
  }
  return false;
}

// Test-only.  Not for production use.  Caller must ensure there are at least 5
// bytes at nal_unit.
uint8_t GetNalUnitType(const uint8_t* nal_unit) {
  // Also works with 4-byte startcodes.
  static const uint8_t start_code[3] = {0, 0, 1};
  uint8_t* next_start = static_cast<uint8_t*>(memmem(nal_unit, 5, start_code, sizeof(start_code))) +
                        sizeof(start_code);
  return *next_start & 0xf;
}

struct __attribute__((__packed__)) IvfHeader {
  uint32_t signature;
  uint16_t version;
  uint16_t header_length;
  uint32_t fourcc;
  uint16_t width;
  uint16_t height;
  uint32_t frame_rate;
  uint32_t time_scale;
  uint32_t frame_count;
  uint32_t unused;
};

struct __attribute__((__packed__)) IvfFrameHeader {
  uint32_t size_bytes;
  uint64_t presentation_timestamp;
};

enum class Format {
  kH264,
  // This uses the multi-instance h.264 decoder.
  kH264Multi,
  kVp9,
};

const uint8_t kSliceNalUnitTypes[] = {1, 2, 3, 4, 5, 19, 20, 21};
bool IsSliceNalUnitType(uint8_t nal_unit_type) {
  for (uint8_t type : kSliceNalUnitTypes) {
    if (type == nal_unit_type) {
      return true;
    }
  }
  return false;
}

}  // namespace

// Payload data for bear.h264 is 00 00 00 01 start code before each NAL, with
// SPS / PPS NALs and also frame NALs.  We deliver to Codec NAL-by-NAL without
// the start code.
//
// Since the .h264 file has SPS + PPS NALs in addition to frame NALs, we don't
// use oob_bytes for this stream.
//
// TODO(dustingreen): Determine for .mp4 or similar which don't have SPS / PPS
// in band whether .mp4 provides ongoing OOB data, or just at the start, and
// document in codec.fidl how that's to be handled.
//
// Returns how many input packets queued with a PTS.
uint64_t QueueH264Frames(CodecClient* codec_client, InStreamPeeker* in_stream,
                         uint64_t stream_lifetime_ordinal, uint64_t input_pts_counter_start,
                         InputCopier* tvp, const UseVideoDecoderTestParams* test_params) {
  // Raw .h264 has start code 00 00 01 or 00 00 00 01 before each NAL, and
  // the start codes don't alias in the middle of NALs, so we just scan
  // for NALs and send them in to the decoder.
  uint64_t input_pts_counter = input_pts_counter_start;
  uint64_t frame_count = 0;
  std::vector<uint8_t> accumulator;
  auto queue_access_unit = [&codec_client, tvp, stream_lifetime_ordinal, test_params,
                            &input_pts_counter, &frame_count,
                            &accumulator](uint8_t* bytes, size_t byte_count) -> bool {
    size_t insert_offset = accumulator.size();
    size_t new_size = insert_offset + byte_count;
    if (accumulator.capacity() < new_size) {
      size_t new_capacity = std::max(accumulator.capacity() * 2, new_size);
      accumulator.reserve(new_capacity);
    }
    accumulator.resize(insert_offset + byte_count);
    memcpy(accumulator.data() + insert_offset, bytes, byte_count);

    size_t start_code_size_bytes = 0;
    ZX_ASSERT(is_start_code(bytes, byte_count, &start_code_size_bytes));
    ZX_ASSERT(start_code_size_bytes < byte_count);
    uint8_t nal_unit_type = bytes[start_code_size_bytes] & 0x1f;
    if (!kH264SeparateSpsPps && !IsSliceNalUnitType(nal_unit_type)) {
      return true;
    }

    auto orig_bytes = bytes;
    bytes = accumulator.data();
    byte_count = accumulator.size();
    auto clear_accumulator = fit::defer([&accumulator] { accumulator.clear(); });

    size_t bytes_so_far = 0;
    // printf("queuing offset: %ld byte_count: %zu\n", bytes -
    // input_bytes.get(), byte_count);
    while (bytes_so_far != byte_count) {
      VLOGF("BlockingGetFreeInputPacket()...");
      std::unique_ptr<fuchsia::media::Packet> packet = codec_client->BlockingGetFreeInputPacket();
      if (!packet) {
        return false;
      }
      VLOGF("BlockingGetFreeInputPacket() done");

      if (!packet->has_header()) {
        Exit("broken server sent packet without header");
      }

      if (!packet->header().has_packet_index()) {
        Exit("broken server sent packet without packet index");
      }

      // For input we do buffer_index == packet_index.
      const CodecBuffer& buffer = codec_client->BlockingGetFreeInputBufferForPacket(packet.get());
      ZX_ASSERT(packet->buffer_index() == buffer.buffer_index());
      uint32_t padding_length = tvp ? tvp->PaddingLength() : 0;
      size_t bytes_to_copy =
          std::min(byte_count - bytes_so_far, buffer.size_bytes() - padding_length);

      // Force some frames to split across packet boundary.
      //
      // TODO(fxb/13483): Also cover more than one frame in a packet, and split headers.
      //
      // TODO(fxb/13483): Enable testing frames split across packets once SW decode can do that, or
      // have this be gated on whether capability was requested of decoder and try requesting this
      // capability then fall back to not this capability.
      (void)kMaxFrameBytesPerPacket;
      // bytes_to_copy = std::min(bytes_to_copy, kMaxFrameBytesPerPacket);

      packet->set_stream_lifetime_ordinal(stream_lifetime_ordinal);
      packet->set_start_offset(0);
      packet->set_valid_length_bytes(bytes_to_copy);

      if (bytes_so_far == 0) {
        uint8_t nal_unit_type = GetNalUnitType(orig_bytes);
        if (IsSliceNalUnitType(nal_unit_type)) {
          packet->set_timestamp_ish(input_pts_counter++);
        }
      }

      packet->set_start_access_unit(bytes_so_far == 0);
      packet->set_known_end_access_unit(bytes_so_far + bytes_to_copy == byte_count);
      if (tvp) {
        TEEC_Result result = tvp->DecryptVideo(bytes + bytes_so_far, bytes_to_copy, buffer.vmo());
        ZX_ASSERT(result == TEEC_SUCCESS);
      } else {
        memcpy(buffer.base(), bytes + bytes_so_far, bytes_to_copy);
      }
      codec_client->QueueInputPacket(std::move(packet));
      bytes_so_far += bytes_to_copy;
    }
    if (IsSliceNalUnitType(nal_unit_type)) {
      frame_count++;
    }
    if (frame_count == test_params->frame_count) {
      return false;
    }
    return true;
  };

  // Let caller-provided in_stream drive how far ahead we peek.  If it's not far
  // enough to find a start code or the EOS, then we'll error out.
  uint32_t max_peek_bytes = in_stream->max_peek_bytes();
  // default -1
  int64_t input_stop_stream_after_frame_ordinal =
      test_params->input_stop_stream_after_frame_ordinal;
  int64_t stream_frame_ordinal = 0;
  while (true) {
    // Until clang-tidy correctly interprets Exit(), this "= 0" satisfies it.
    size_t start_code_size_bytes = 0;
    uint32_t actual_peek_bytes;
    uint8_t* peek;
    VLOGF("PeekBytes()...");
    zx_status_t status = in_stream->PeekBytes(max_peek_bytes, &actual_peek_bytes, &peek,
                                              zx::deadline_after(kInStreamDeadlineDuration));
    ZX_ASSERT(status == ZX_OK);
    VLOGF("PeekBytes() done");
    if (actual_peek_bytes == 0) {
      // Out of input.  Not an error.  No more input AUs.
      ZX_DEBUG_ASSERT(in_stream->eos_position_known() &&
                      in_stream->cursor_position() == in_stream->eos_position());
      break;
    }
    if (!is_start_code(&peek[0], actual_peek_bytes, &start_code_size_bytes)) {
      for (uint32_t i = 0; i < 64; ++i) {
        LOGF("peek[%u] == 0x%x", i, peek[i]);
      }
      char buf[65] = {};
      memcpy(&buf[0], &peek[0], 64);
      buf[64] = 0;
      LOGF("peek[0..64]: %s", buf);
      if (in_stream->cursor_position() == 0) {
        Exit(
            "Didn't find a start code at the start of the file, and this "
            "example doesn't scan forward (for now).");
      } else {
        Exit(
            "Fell out of sync somehow - previous NAL offset + previous "
            "NAL length not a start code.");
      }
    }
    if (in_stream->eos_position_known() &&
        in_stream->cursor_position() + start_code_size_bytes == in_stream->eos_position()) {
      Exit("Start code at end of file unexpected");
    }
    size_t nal_start_offset = start_code_size_bytes;
    // Scan for end of NAL.  The end of NAL can be because we're out of peeked
    // data, or because we hit another start code.
    size_t find_end_iter = nal_start_offset;
    size_t ignore_start_code_size_bytes;
    while (find_end_iter <= actual_peek_bytes &&
           !is_start_code(&peek[find_end_iter], actual_peek_bytes - find_end_iter,
                          &ignore_start_code_size_bytes)) {
      find_end_iter++;
    }
    ZX_DEBUG_ASSERT(find_end_iter <= actual_peek_bytes);
    if (find_end_iter == nal_start_offset) {
      Exit("Two adjacent start codes unexpected.");
    }
    ZX_DEBUG_ASSERT(find_end_iter > nal_start_offset);
    size_t nal_length = find_end_iter - nal_start_offset;
    if (!queue_access_unit(&peek[0], start_code_size_bytes + nal_length)) {
      // only reached on error
      break;
    }

    // start code + NAL payload
    VLOGF("TossPeekedBytes()...");
    in_stream->TossPeekedBytes(start_code_size_bytes + nal_length);
    VLOGF("TossPeekedBytes() done");

    if (stream_frame_ordinal == input_stop_stream_after_frame_ordinal) {
      break;
    }
    stream_frame_ordinal++;
  }

  return input_pts_counter - input_pts_counter_start;
}

uint64_t QueueVp9Frames(CodecClient* codec_client, InStreamPeeker* in_stream,
                        uint64_t stream_lifetime_ordinal, uint64_t input_pts_counter_start,
                        InputCopier* tvp, const UseVideoDecoderTestParams* test_params) {
  // default -1
  const int64_t skip_frame_ordinal = test_params->skip_frame_ordinal;
  int64_t input_pts_counter = input_pts_counter_start;
  auto queue_access_unit = [&codec_client, in_stream, stream_lifetime_ordinal, &input_pts_counter,
                            tvp, skip_frame_ordinal](size_t byte_count) {
    std::unique_ptr<fuchsia::media::Packet> packet = codec_client->BlockingGetFreeInputPacket();
    if (!packet) {
      fprintf(stderr, "Returning because failed to get input packet\n");
      return false;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // No more return false from here down.  Before we return true, we must have consumed the input
    // data, and incremented the input_frame_ordinal, and returned the input packet to the
    // codec_client.  The codec_client only wants the input packet back after its been filled out
    // completely.
    ////////////////////////////////////////////////////////////////////////////////////////////////
    auto do_not_return_early_interval = fit::defer([] {
      ZX_PANIC("don't return early until packet is set up and returned to codec_client\n");
    });
    auto increment_input_pts_counter = fit::defer([&input_pts_counter] { input_pts_counter++; });

    ZX_ASSERT(packet->has_header());
    ZX_ASSERT(packet->header().has_packet_index());
    const CodecBuffer& buffer = codec_client->BlockingGetFreeInputBufferForPacket(packet.get());
    ZX_ASSERT(packet->buffer_index() == buffer.buffer_index());
    // VP9 decoder doesn't yet support splitting access units into multiple
    // packets.
    if (byte_count > buffer.size_bytes()) {
      fprintf(stderr,
              "buffer_count >= buffer.size_bytes() - byte_count: %lu buffer.size_bytes(): %lu\n",
              byte_count, buffer.size_bytes());
    }
    ZX_ASSERT(byte_count <= buffer.size_bytes());

    // Check that we don't waste contiguous space on non-secure VP9 input buffers.
    ZX_ASSERT(!buffer.is_physically_contiguous() || tvp);
    packet->set_stream_lifetime_ordinal(stream_lifetime_ordinal);
    packet->set_start_offset(0);
    packet->set_valid_length_bytes(byte_count);

    // We don't use frame_header->presentation_timestamp, because we want to
    // send through frame index in timestamp_ish field instead, for consistency
    // with .h264 files which don't have timestamps in them, and so tests can
    // assume frame index as timestamp_ish on output.
    packet->set_timestamp_ish(input_pts_counter);

    packet->set_start_access_unit(true);
    packet->set_known_end_access_unit(true);

    uint32_t actual_bytes_read;
    std::unique_ptr<uint8_t[]> bytes;
    uint8_t* read_address = nullptr;

    if (tvp) {
      bytes = std::make_unique<uint8_t[]>(byte_count);
      read_address = bytes.get();
    } else {
      read_address = buffer.base();
    }

    zx_status_t status =
        in_stream->ReadBytesComplete(byte_count, &actual_bytes_read, read_address,
                                     zx::deadline_after(kInStreamDeadlineDuration));
    ZX_ASSERT(status == ZX_OK);
    if (actual_bytes_read < byte_count) {
      Exit("Frame truncated.");
    }
    ZX_DEBUG_ASSERT(actual_bytes_read == byte_count);

    /////////////////////////////////////////////////////////////////////////////////
    // Switch from not being able to return early to being able to return true early.
    /////////////////////////////////////////////////////////////////////////////////
    do_not_return_early_interval.cancel();
    auto do_not_queue_input_packet_after_all = fit::defer([codec_client, &packet] {
      codec_client->DoNotQueueInputPacketAfterAll(std::move(packet));
    });

    if (input_pts_counter == skip_frame_ordinal) {
      LOGF("skipping input frame: %" PRId64, input_pts_counter);
      // ~do_not_queue_input_packet_after_all, ~increment_input_pts_counter
      return true;
    }

    if (tvp) {
      VLOGF("before DecryptVideo...");
      TEEC_Result result = tvp->DecryptVideo(bytes.get(), byte_count, buffer.vmo());
      VLOGF("after DecryptVideo");
      ZX_ASSERT(result == TEEC_SUCCESS);
    }

    do_not_queue_input_packet_after_all.cancel();
    codec_client->QueueInputPacket(std::move(packet));

    // ~increment_input_pts_counter
    return true;
  };
  IvfHeader header;
  uint32_t actual_bytes_read;
  zx_status_t status = in_stream->ReadBytesComplete(sizeof(header), &actual_bytes_read,
                                                    reinterpret_cast<uint8_t*>(&header),
                                                    zx::deadline_after(kInStreamDeadlineDuration));
  // This could fail if a remote-source stream breaks.
  ZX_ASSERT(status == ZX_OK);
  // This could fail if the input is too short.
  ZX_ASSERT(actual_bytes_read == sizeof(header));
  size_t remaining_header_length = header.header_length - sizeof(header);
  // We're not interested in any remaining portion of the header, but we should
  // skip the rest of the header, if any.
  if (remaining_header_length) {
    uint8_t toss_buffer[1024];
    while (remaining_header_length != 0) {
      uint32_t bytes_to_read = std::min(sizeof(toss_buffer), remaining_header_length);
      uint32_t actual_bytes_read;
      status = in_stream->ReadBytesComplete(bytes_to_read, &actual_bytes_read, &toss_buffer[0],
                                            zx::deadline_after(kInStreamDeadlineDuration));
      ZX_ASSERT(status == ZX_OK);
      ZX_ASSERT(actual_bytes_read == bytes_to_read);
      remaining_header_length -= actual_bytes_read;
    }
  }
  ZX_DEBUG_ASSERT(!remaining_header_length);
  // default -1
  int64_t input_stop_stream_after_frame_ordinal =
      test_params->input_stop_stream_after_frame_ordinal;
  int64_t stream_frame_ordinal = 0;
  while (true) {
    IvfFrameHeader frame_header;
    status = in_stream->ReadBytesComplete(sizeof(frame_header), &actual_bytes_read,
                                          reinterpret_cast<uint8_t*>(&frame_header),
                                          zx::deadline_after(kInStreamDeadlineDuration));
    ZX_ASSERT(status == ZX_OK);
    if (actual_bytes_read == 0) {
      // No more frames.  That's fine.
      break;
    }
    if (actual_bytes_read < sizeof(frame_header)) {
      Exit("Frame header truncated.");
    }
    ZX_DEBUG_ASSERT(actual_bytes_read == sizeof(frame_header));
    LOGF("input stream: %" PRIu64 " stream_frame_ordinal: %" PRId64 " input_pts_counter: %" PRIu64
         " frame_header.size_bytes: %u",
         stream_lifetime_ordinal, stream_frame_ordinal, input_pts_counter, frame_header.size_bytes);
    if (!queue_access_unit(frame_header.size_bytes)) {
      // can be fine in case of vp9 input fuzzing test
      break;
    }

    if (stream_frame_ordinal == input_stop_stream_after_frame_ordinal) {
      break;
    }
    stream_frame_ordinal++;
  }

  return input_pts_counter - input_pts_counter_start;
}

static void use_video_decoder(Format format, UseVideoDecoderParams params) {
  VLOGF("use_video_decoder()");

  const UseVideoDecoderTestParams default_test_params;
  if (!params.test_params) {
    params.test_params = &default_test_params;
  }
  params.test_params->Validate();

  VLOGF("before CodecClient::CodecClient()...");
  CodecClient codec_client(params.fidl_loop, params.fidl_thread, std::move(params.sysmem));
  // no effect if 0
  codec_client.SetMinOutputBufferSize(params.min_output_buffer_size);
  // no effect if 0
  codec_client.SetMinOutputBufferCount(params.min_output_buffer_count);
  codec_client.set_is_output_secure(params.is_secure_output);
  codec_client.set_is_input_secure(params.is_secure_input);
  codec_client.set_in_lax_mode(params.lax_mode);

  std::string mime_type;
  switch (format) {
    case Format::kH264:
      mime_type = "video/h264";
      break;

    case Format::kH264Multi:
      mime_type = "video/h264-multi";
      break;

    case Format::kVp9:
      mime_type = "video/vp9";
      break;
  }
  if (params.test_params->mime_type) {
    mime_type = params.test_params->mime_type.value();
  }

  async::PostTask(
      params.fidl_loop->dispatcher(),
      [&params, codec_client_request = codec_client.GetTheRequestOnce(), mime_type]() mutable {
        VLOGF("before codec_factory->CreateDecoder() (async)");
        fuchsia::media::FormatDetails input_details;
        input_details.set_format_details_version_ordinal(0);
        input_details.set_mime_type(mime_type.c_str());
        fuchsia::mediacodec::CreateDecoder_Params decoder_params;
        decoder_params.set_input_details(std::move(input_details));
        // This is required for timestamp_ish values to transit the
        // Codec.
        //
        // TODO(fxb/57706): We shouldn't need to promise this to have PTS(s) flow through.
        decoder_params.set_promise_separate_access_units_on_input(true);
        if (params.is_secure_output) {
          decoder_params.set_secure_output_mode(fuchsia::mediacodec::SecureMemoryMode::ON);
        }
        if (params.is_secure_input) {
          decoder_params.set_secure_input_mode(fuchsia::mediacodec::SecureMemoryMode::ON);
        }
        params.codec_factory->CreateDecoder(std::move(decoder_params),
                                            std::move(codec_client_request));
      });

  VLOGF("before codec_client.Start()...");
  // This does a Sync(), so after this we can drop the CodecFactory without it
  // potentially cancelling our Codec create.
  codec_client.Start();

  // We don't need the CodecFactory any more, and at this point any Codec
  // creation errors have had a chance to arrive via the
  // codec_factory.set_error_handler() lambda.
  //
  // Unbind() is only safe to call on the interfaces's dispatcher thread.  We
  // also want to block the current thread until this is done, to avoid
  // codec_factory potentially disappearing before this posted work finishes.
  OneShotEvent unbind_done_event;
  async::PostTask(params.fidl_loop->dispatcher(), [&params, &unbind_done_event] {
    params.codec_factory.Unbind();
    unbind_done_event.Signal();
    // codec_factory and unbind_done_event are potentially gone by this
    // point.
  });
  unbind_done_event.Wait();

  VLOGF("before starting in_thread...");
  std::unique_ptr<std::thread> in_thread = std::make_unique<std::thread>(
      [&codec_client, in_stream = params.in_stream, format, copier = params.input_copier,
       test_params = params.test_params]() {
        VLOGF("in_thread start");
        // default 1
        const uint32_t loop_stream_count = test_params->loop_stream_count;
        // default 2
        const uint64_t keep_stream_modulo = test_params->keep_stream_modulo;
        uint64_t stream_lifetime_ordinal = kStreamLifetimeOrdinal;
        uint64_t input_frame_pts_counter = 0;
        uint32_t frames_queued = 0;
        for (uint32_t loop_ordinal = 0; loop_ordinal < loop_stream_count;
             ++loop_ordinal, stream_lifetime_ordinal += 2) {
          switch (format) {
            case Format::kH264:
            case Format::kH264Multi:
              frames_queued = QueueH264Frames(&codec_client, in_stream, stream_lifetime_ordinal,
                                              input_frame_pts_counter, copier, test_params);
              break;

            case Format::kVp9:
              frames_queued = QueueVp9Frames(&codec_client, in_stream, stream_lifetime_ordinal,
                                             input_frame_pts_counter, copier, test_params);
              break;
          }

          // Send through QueueInputEndOfStream().
          VLOGF("QueueInputEndOfStream() - stream_lifetime_ordinal: %" PRIu64,
                stream_lifetime_ordinal);
          // For debugging a flake:
          if (test_params->loop_stream_count > 1) {
            LOGF("QueueInputEndOfStream() - stream_lifetime_ordinal: %" PRIu64,
                 stream_lifetime_ordinal);
          }
          codec_client.QueueInputEndOfStream(stream_lifetime_ordinal);

          if (stream_lifetime_ordinal % keep_stream_modulo == 1) {
            // We flush and close to run the handling code server-side.  However, we don't
            // yet verify that this successfully achieves what it says.
            VLOGF("FlushEndOfStreamAndCloseStream() - stream_lifetime_ordinal: %" PRIu64,
                  stream_lifetime_ordinal);
            // For debugging a flake:
            if (test_params->loop_stream_count > 1) {
              LOGF("FlushEndOfStreamAndCloseStream() - stream_lifetime_ordinal: %" PRIu64,
                   stream_lifetime_ordinal);
            }
            codec_client.FlushEndOfStreamAndCloseStream(stream_lifetime_ordinal);

            // Stitch together the PTS values of the streams which we're keeping.
            input_frame_pts_counter += frames_queued;
          }

          if (loop_ordinal + 1 != loop_stream_count) {
            zx_status_t status =
                in_stream->ResetToStart(zx::deadline_after(kInStreamDeadlineDuration));
            ZX_ASSERT(status == ZX_OK);
          }
        }
        VLOGF("in_thread done");
      });

  // Separate thread to process the output.
  //
  // codec_client outlives the thread (and for separate reasons below, all the
  // frame_sink activity started by out_thread).
  std::unique_ptr<std::thread> out_thread = std::make_unique<std::thread>([&codec_client,
                                                                           &params]() {
    VLOGF("out_thread start");
    // We allow the server to send multiple output constraint updates if it
    // wants; see implementation of BlockingGetEmittedOutput() which will hide
    // multiple constraint updates before the first packet from this code.  In
    // contrast we assert if the server sends multiple format updates with no
    // packets in between since that's not compliant with the protocol rules.
    std::shared_ptr<const fuchsia::media::StreamOutputFormat> prev_stream_format;
    const fuchsia::media::VideoUncompressedFormat* raw = nullptr;
    while (true) {
      VLOGF("BlockingGetEmittedOutput()...");
      std::unique_ptr<CodecOutput> output = codec_client.BlockingGetEmittedOutput();
      VLOGF("BlockingGetEmittedOutput() done");
      if (!output) {
        return;
      }
      if (output->stream_lifetime_ordinal() % 2 == 0) {
        Exit(
            "server emitted a stream_lifetime_ordinal that client didn't set "
            "on any input");
      }
      if (output->end_of_stream()) {
        VLOGF("output end_of_stream() - stream_lifetime_ordinal: %" PRIu64,
              output->stream_lifetime_ordinal());
        // For debugging a flake:
        if (params.test_params->loop_stream_count > 1) {
          LOGF("output end_of_stream() - stream_lifetime_ordinal: %" PRIu64,
               output->stream_lifetime_ordinal());
        }
        // default 1
        const int64_t loop_stream_count = params.test_params->loop_stream_count;
        const uint64_t max_stream_lifetime_ordinal = (loop_stream_count - 1) * 2 + 1;
        if (output->stream_lifetime_ordinal() != max_stream_lifetime_ordinal) {
          continue;
        }
        VLOGF("done with output - stream_lifetime_ordinal: %" PRIu64,
              output->stream_lifetime_ordinal());
        // For debugging a flake:
        if (params.test_params->loop_stream_count > 1) {
          LOGF("done with output - stream_lifetime_ordinal: %" PRIu64,
               output->stream_lifetime_ordinal());
        }
        // Just "break;" would be more fragile under code modification.
        goto end_of_output;
      }

      const fuchsia::media::Packet& packet = output->packet();

      if (!packet.has_header()) {
        // The server should not generate any empty packets.
        Exit("broken server sent packet without header");
      }

      // cleanup can run on any thread, and codec_client.RecycleOutputPacket()
      // is ok with that.  In addition, cleanup can run after codec_client is
      // gone, since we don't block return from use_video_decoder() on Scenic
      // actually freeing up all previously-queued frames.
      auto cleanup =
          fit::defer([&codec_client, packet_header = fidl::Clone(packet.header())]() mutable {
            // Using an auto call for this helps avoid losing track of the
            // output_buffer.
            codec_client.RecycleOutputPacket(std::move(packet_header));
          });
      std::shared_ptr<const fuchsia::media::StreamOutputFormat> format = output->format();

      if (!packet.has_buffer_index()) {
        // The server should not generate any empty packets.
        Exit("broken server sent packet without buffer index");
      }

      // This will remain live long enough because this thread is the only
      // thread that re-allocates output buffers.
      const CodecBuffer& buffer = codec_client.GetOutputBufferByIndex(packet.buffer_index());

      ZX_ASSERT(!prev_stream_format ||
                (prev_stream_format->has_format_details() &&
                 prev_stream_format->format_details().format_details_version_ordinal()));
      if (!format->has_format_details()) {
        Exit("!format->has_format_details()");
      }
      if (!format->format_details().has_format_details_version_ordinal()) {
        Exit("!format->format_details().has_format_details_version_ordinal()");
      }

      if (!packet.has_valid_length_bytes() || packet.valid_length_bytes() == 0) {
        // The server should not generate any empty packets.
        Exit("broken server sent empty packet");
      }

      if (!packet.has_start_offset()) {
        // The server should not generate any empty packets.
        Exit("broken server sent packet without start offset");
      }

      // We have a non-empty packet of the stream.

      if (!prev_stream_format || prev_stream_format.get() != format.get()) {
        VLOGF("handling output format");
        // Every output has a format.  This happens exactly once.
        prev_stream_format = format;

        ZX_ASSERT(format->format_details().has_domain());

        if (!format->has_format_details()) {
          Exit("!format_details");
        }

        const fuchsia::media::FormatDetails& format_details = format->format_details();
        if (!format_details.has_domain()) {
          Exit("!format.domain");
        }

        if (!format_details.domain().is_video()) {
          Exit("!format.domain.is_video()");
        }
        const fuchsia::media::VideoFormat& video_format = format_details.domain().video();
        if (!video_format.is_uncompressed()) {
          Exit("!video.is_uncompressed()");
        }

        raw = &video_format.uncompressed();
        switch (raw->fourcc) {
          case make_fourcc('N', 'V', '1', '2'): {
            size_t y_size = raw->primary_height_pixels * raw->primary_line_stride_bytes;
            if (raw->secondary_start_offset < y_size) {
              Exit("raw.secondary_start_offset < y_size");
            }
            // NV12 requires UV be same line stride as Y.
            size_t total_size = raw->secondary_start_offset +
                                raw->primary_height_pixels / 2 * raw->primary_line_stride_bytes;
            if (packet.valid_length_bytes() < total_size) {
              Exit(
                  "packet.valid_length_bytes < total_size (1) - valid_length_bytes: %u total_size: "
                  "%lu",
                  packet.valid_length_bytes(), total_size);
            }
            break;
          }
          case make_fourcc('Y', 'V', '1', '2'): {
            size_t y_size = raw->primary_height_pixels * raw->primary_line_stride_bytes;
            size_t v_size = raw->secondary_height_pixels * raw->secondary_line_stride_bytes;
            size_t u_size = v_size;
            size_t total_size = y_size + u_size + v_size;

            if (packet.valid_length_bytes() < total_size) {
              Exit("packet.valid_length_bytes < total_size (2)");
            }

            if (raw->secondary_start_offset < y_size) {
              Exit("raw.secondary_start_offset < y_size");
            }

            if (raw->tertiary_start_offset < y_size + v_size) {
              Exit("raw.tertiary_start_offset < y_size + v_size");
            }
            break;
          }
          default:
            Exit("fourcc != NV12 && fourcc != YV12");
        }
      }

      if (params.emit_frame) {
        // i420_bytes is in I420 format - Y plane first, then U plane, then V
        // plane.  The U and V planes are half size in both directions.  Each
        // plane is 8 bits per sample.
        uint32_t i420_stride = fbl::round_up(raw->primary_display_width_pixels, 2u);
        // When width is odd, we want a chroma sample for the right-most luma.
        uint32_t uv_width = (raw->primary_display_width_pixels + 1) / 2;
        // When height is odd, we want a chroma sample for the bottom-most luma.
        uint32_t uv_height = (raw->primary_display_height_pixels + 1) / 2;
        uint32_t uv_stride = i420_stride / 2;
        std::unique_ptr<uint8_t[]> i420_bytes;
        if (kVerifySecureOutput || !params.is_secure_output) {
          i420_bytes = std::make_unique<uint8_t[]>(
              i420_stride * raw->primary_display_height_pixels + uv_stride * uv_height * 2);
          switch (raw->fourcc) {
            case make_fourcc('N', 'V', '1', '2'): {
              // Y
              uint8_t* y_src = buffer.base() + packet.start_offset() + raw->primary_start_offset;
              uint8_t* y_dst = i420_bytes.get();
              for (uint32_t y_iter = 0; y_iter < raw->primary_display_height_pixels; y_iter++) {
                memcpy(y_dst, y_src, raw->primary_display_width_pixels);
                y_src += raw->primary_line_stride_bytes;
                y_dst += i420_stride;
              }
              // UV
              uint8_t* uv_src = buffer.base() + packet.start_offset() + raw->secondary_start_offset;
              uint8_t* u_dst_line = y_dst;
              uint8_t* v_dst_line = u_dst_line + uv_stride * uv_height;
              for (uint32_t uv_iter = 0; uv_iter < uv_height; uv_iter++) {
                uint8_t* u_dst = u_dst_line;
                uint8_t* v_dst = v_dst_line;
                for (uint32_t uv_line_iter = 0; uv_line_iter < uv_width; ++uv_line_iter) {
                  *u_dst++ = uv_src[uv_line_iter * 2];
                  *v_dst++ = uv_src[uv_line_iter * 2 + 1];
                }
                uv_src += raw->primary_line_stride_bytes;
                u_dst_line += uv_stride;
                v_dst_line += uv_stride;
              }
              break;
            }
            case make_fourcc('Y', 'V', '1', '2'): {
              // Y
              uint8_t* y_src = buffer.base() + packet.start_offset() + raw->primary_start_offset;
              uint8_t* y_dst = i420_bytes.get();
              for (uint32_t y_iter = 0; y_iter < raw->primary_display_height_pixels; y_iter++) {
                memcpy(y_dst, y_src, raw->primary_display_width_pixels);
                y_src += raw->primary_line_stride_bytes;
                y_dst += i420_stride;
              }
              // UV
              uint8_t* v_src = buffer.base() + packet.start_offset() + raw->primary_start_offset +
                               raw->primary_line_stride_bytes * raw->primary_height_pixels;
              uint8_t* u_src =
                  v_src + (raw->primary_line_stride_bytes / 2) * (raw->primary_height_pixels / 2);
              uint8_t* u_dst = y_dst;
              uint8_t* v_dst = u_dst + uv_stride * uv_height;
              for (uint32_t uv_iter = 0; uv_iter < uv_height; uv_iter++) {
                memcpy(u_dst, u_src, uv_width);
                memcpy(v_dst, v_src, uv_width);
                u_dst += uv_stride;
                v_dst += uv_stride;
                u_src += raw->primary_line_stride_bytes / 2;
                v_src += raw->primary_line_stride_bytes / 2;
              }
              break;
            }
            default:
              Exit("Feeding EmitFrame not yet implemented for fourcc: %s",
                   fourcc_to_string(raw->fourcc).c_str());
          }
        }
        params.emit_frame(output->stream_lifetime_ordinal(), i420_bytes.get(),
                          raw->primary_display_width_pixels, raw->primary_display_height_pixels,
                          i420_stride, packet.has_timestamp_ish(),
                          packet.has_timestamp_ish() ? packet.timestamp_ish() : 0);
      }

      if (params.frame_sink) {
        async::PostTask(
            params.fidl_loop->dispatcher(),
            [image_id = packet.header().packet_index() + kFirstValidImageId, &params,
             &vmo = buffer.vmo(),
             vmo_offset = buffer.vmo_offset() + packet.start_offset() + raw->primary_start_offset,
             format, cleanup = std::move(cleanup)]() mutable {
              params.frame_sink->PutFrame(image_id, vmo, vmo_offset, format,
                                          [cleanup = std::move(cleanup)] {
                                            // The ~cleanup can run on any thread (the
                                            // current thread is main_loop's thread),
                                            // and codec_client is ok with that
                                            // (because it switches over to |loop|'s
                                            // thread before sending a Codec message).
                                            //
                                            // ~cleanup
                                          });
            });
      }
      // If we didn't std::move(cleanup) before here, then ~cleanup runs here.
    }
  end_of_output:;
    VLOGF("out_thread done");
    // output thread done
    // ~raw_video_writer
  });

  // decode for a bit...  in_thread, loop, out_thread, and the codec itself are
  // taking care of it.

  // First wait for the input thread to be done feeding input data.  Before the
  // in_thread terminates, it'll have sent in a last empty EOS input buffer.
  VLOGF("before in_thread->join()...");
  in_thread->join();
  VLOGF("after in_thread->join()");

  // The EOS queued as an input buffer should cause the codec to output an EOS
  // output buffer, at which point out_thread should terminate, after it has
  // finalized the output file.
  VLOGF("before out_thread->join()...");
  out_thread->join();
  VLOGF("after out_thread->join()");

  // We wait for frame_sink to return all the frames for these reasons:
  //   * As of this writing, some noisy-in-the-log things can happen in Scenic
  //     if we don't.
  //   * We don't want to cancel display of any frames, because we want to see
  //     the frames on the screen.
  //   * We don't want the |cleanup| to run after codec_client is gone since the
  //     |cleanup| calls codec_client.
  //   * It's easier to grok if activity started by use_h264_decoder() is done
  //     by the time use_h264_decoder() returns, given use_h264_decoder()'s role
  //     as an overall sequencer.
  if (params.frame_sink) {
    OneShotEvent frames_done_event;
    fit::closure on_frames_returned = [&frames_done_event] { frames_done_event.Signal(); };
    async::PostTask(params.fidl_loop->dispatcher(), [frame_sink = params.frame_sink,
                                                     on_frames_returned =
                                                         std::move(on_frames_returned)]() mutable {
      frame_sink->PutEndOfStreamThenWaitForFramesReturnedAsync(std::move(on_frames_returned));
    });
    // The just-posted wait will set frames_done using the main_loop_'s thread,
    // which is not this thread.
    FX_LOGS(INFO) << "waiting for all frames to be returned from Scenic...";
    frames_done_event.Wait(zx::deadline_after(zx::sec(30)));
    FX_LOGS(INFO) << "all frames have been returned from Scenic";
    // Now we know that there are zero frames in frame_sink, including zero
    // frame cleanup(s) in-flight (in the sense of a pending/running cleanup
    // that's touching codec_client to post any new work.  Work already posted
    // via codec_client can still be in flight.  See below.)
  }

  // Close the channels explicitly (just so we can more easily print messages
  // before and after vs. ~codec_client).
  VLOGF("before codec_client stop...");
  codec_client.Stop();
  VLOGF("after codec_client stop.");

  // success
  // ~codec_client
  return;
}

void use_h264_decoder(UseVideoDecoderParams params) {
  use_video_decoder(Format::kH264, std::move(params));
}

void use_h264_multi_decoder(UseVideoDecoderParams params) {
  use_video_decoder(Format::kH264Multi, std::move(params));
}

void use_vp9_decoder(UseVideoDecoderParams params) {
  use_video_decoder(Format::kVp9, std::move(params));
}
