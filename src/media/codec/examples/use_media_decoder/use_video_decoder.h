// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stdint.h>

#include <limits>
#include <variant>

#include <openssl/sha.h>

// We only flush input EOS for streams whose stream_lifetime_ordinal %
// kFlushInputEosStreamLifetimeOrdinalPeriod == 1.
constexpr uint64_t kFlushInputEosStreamLifetimeOrdinalPeriod = 16;

class FrameSink;
class InStreamPeeker;
class InputCopier;

// An EmitFrame is passed I420 frames with stride == width, and with width
// and height being display_width and display_height (not coded_width and
// coded_height).  The width and height must be even.
typedef fit::function<void(uint64_t stream_lifetime_ordinal, uint8_t* i420_data, uint32_t width,
                           uint32_t height, uint32_t stride, bool has_timestamp_ish,
                           uint64_t timestamp_ish)>
    EmitFrame;

// Keep fields in alphabetical order please, other than is_validated_.
struct UseVideoDecoderTestParams final {
  // Let default constructor exist.  This doesn't count as user-declared, so aggregate
  // initialization can still be used.
  UseVideoDecoderTestParams() = default;
  // No copy, assign, or move.  None of this prevents aggregate initialization.
  UseVideoDecoderTestParams(const UseVideoDecoderTestParams& from) = delete;
  UseVideoDecoderTestParams& operator=(const UseVideoDecoderTestParams& from) = delete;
  UseVideoDecoderTestParams(const UseVideoDecoderTestParams&& from) = delete;
  UseVideoDecoderTestParams& operator=(const UseVideoDecoderTestParams&& from) = delete;

  ~UseVideoDecoderTestParams() {
    // Ensure Validate() gets called at least once, if a bit later than ideal.
    Validate();
  }

  // Validate() can be called at any time, preferably before the parameters are used.
  //
  // Validate() is also called from the destructor just in case as a backstop, but the call from the
  // constructor shouldn't be the first call to Validate().  The destructor will catch invalid field
  // values if nothing else blows up before then however.
  void Validate() const {
    if (magic_validated_ == kPrivateMagicValidated) {
      return;
    }

    if (first_expected_output_frame_ordinal != kDefaultFirstExpectedOutputFrameOrdinal) {
      printf("first_expected_output_frame_ordinal: %" PRIu64 "\n",
             first_expected_output_frame_ordinal);
    }
    // All values for first_expected_output_frame_ordinal are valid.

    if (keep_stream_modulo != kDefaultKeepStreamModulo) {
      printf("keep_stream_modulo: %" PRIu64 "\n", keep_stream_modulo);
    }
    ZX_ASSERT(keep_stream_modulo != 0);
    ZX_ASSERT(keep_stream_modulo % 2 == 0);

    if (loop_stream_count != kDefaultLoopStreamCount) {
      printf("loop_stream_count: %u\n", loop_stream_count);
    }
    ZX_ASSERT(loop_stream_count != 0);

    if (skip_frame_ordinal != kDefaultSkipFrameOrdinal) {
      printf("skip_frame_ordinal: %" PRId64 "\n", skip_frame_ordinal);
    }
    ZX_ASSERT(skip_frame_ordinal >= -1);

    magic_validated_ = kPrivateMagicValidated;
  }

  // Client code should not touch this field.  This field can't be protected or private without
  // preventing aggregate initialization, so client code just needs to avoid initializing this
  // field (to anything).  Client code should pretend that client code can't possibly guess what
  // kPrivateMagicValidated is.
  //
  // When set to kPrivateMagicValidated, all other fields have been validated.  Else other fields
  // have not been validated.
  mutable uint64_t magic_validated_ = 0;

  // By default, the stream doesn't stop early.
  int64_t input_stop_stream_after_frame_ordinal = -1;

  // The first output frame timestamp_ish that's expected on output.  PTS values before this are not
  // expected.
  //
  // For example if skip_frame_ordinal 0 is used, several frames after that are also skipped until
  // the next keyframe, so first_expected_output_frame_ordinal can be set to the PTS of the next
  // keyframe.
  //
  // By default PTS 0 is expected.
  static constexpr uint64_t kDefaultFirstExpectedOutputFrameOrdinal = 0;
  uint64_t first_expected_output_frame_ordinal = kDefaultFirstExpectedOutputFrameOrdinal;

  // If stream_lifetime_ordinal % keep_stream_modulo is 1, the input stream is flushed after
  // queueing input EOS, so that any subsequent stream switch won't result in any discarded data
  // from the flushed stream.
  //
  // By setting this to an even number larger than 2, some streams don't get flushed, which allows a
  // test to cover that discard doesn't cause problems.
  //
  // By default every stream is flushed.
  static constexpr uint64_t kDefaultKeepStreamModulo = 2;
  uint64_t keep_stream_modulo = kDefaultKeepStreamModulo;

  // If >1, loops through the input data this many times, each time using a new stream with new
  // stream_lifetime_ordinal.
  //
  // 0 is invalid.
  //
  // By default, there's only one stream.
  static constexpr uint32_t kDefaultLoopStreamCount = 1;
  uint32_t loop_stream_count = kDefaultLoopStreamCount;

  // If >= 0, skips any input NAL with PTS == skip_frame_ordinal.
  //
  // -1 is the only valid negative value.
  //
  // By default, no input NALs are skipped due to this parameter.
  static constexpr int64_t kDefaultSkipFrameOrdinal = -1;
  int64_t skip_frame_ordinal = kDefaultSkipFrameOrdinal;

  // This many frames get queued then stop queuing frames.
  uint64_t frame_count = std::numeric_limits<uint64_t>::max();

  // nullopt means no override
  std::optional<std::string> mime_type;

 private:
  // Client code should not exploit knowledge of this value, and should not directly initialize or
  // directly set magic_validated_ to any value.
  static constexpr uint64_t kPrivateMagicValidated = 0xC001DECAFC0DE;
};

struct UseVideoDecoderParams {
  // the loop created and run/started by main().  The codec_factory is
  //     and sysmem are bound to fidl_loop->dispatcher().
  async::Loop* fidl_loop{};
  // the thread on which fidl_loop activity runs.
  thrd_t fidl_thread{};
  // codec_factory to take ownership of, use, and close by the
  //     time the function returns.
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
  InStreamPeeker* in_stream = nullptr;
  InputCopier* input_copier = nullptr;
  uint64_t min_output_buffer_size = 0;
  uint32_t min_output_buffer_count = 0;
  bool is_secure_output = false;
  bool is_secure_input = false;
  bool lax_mode = false;
  // if not nullptr, send each frame to this FrameSink, which will
  //     call back when the frame has been released by the sink.
  FrameSink* frame_sink;
  // if set, is called to emit each frame in i420 format + timestamp
  //     info.
  EmitFrame emit_frame;
  const UseVideoDecoderTestParams* test_params = nullptr;
};
// use_h264_decoder()
//
// If anything goes wrong, exit(-1) is used directly (until we have any reason
// to do otherwise).
//
// On success, the return value is the sha256 of the output data. This is
// intended as a golden-file value when this function is used as part of a test.
// This sha256 value accounts for all the output payload data and also the
// output format parameters. When the same input file is decoded we expect the
// sha256 to be the same.
//
void use_h264_decoder(UseVideoDecoderParams params);

// The same as use_h264_decoder, but use the multi-instance decoder driver.
void use_h264_multi_decoder(UseVideoDecoderParams params);

// The same as use_h264_decoder, but for a VP9 file wrapped in an IVF container.
void use_vp9_decoder(UseVideoDecoderParams params);

// Common function pointer type shared by use_h264_decoder, use_vp9_decoder.
typedef void (*UseVideoDecoderFunction)(UseVideoDecoderParams params);

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_USE_VIDEO_DECODER_H_
