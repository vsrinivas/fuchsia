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

struct FrameToCompare;

// Keep fields in alphabetical order please, other than is_validated_.
struct UseVideoDecoderTestParams final {
  ~UseVideoDecoderTestParams() {
    // Ensure Validate() gets called at least once, if a bit later than ideal.
    Validate();
  }

  UseVideoDecoderTestParams Clone() const {
    UseVideoDecoderTestParams result = *this;
    result.magic_validated_ = 0;
    return result;
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

    if (reset_hash_each_iteration != kDefaultResetHashEachIteration) {
      printf("reset_hash_each_iteration: %u\n", reset_hash_each_iteration);
    }

    if (skip_frame_ordinal != kDefaultSkipFrameOrdinal) {
      printf("skip_frame_ordinal: %" PRId64 "\n", skip_frame_ordinal);
    }
    ZX_ASSERT(skip_frame_ordinal >= -1);

    if (max_num_reorder_frames_threshold != kDefaultMaxNumReorderFramesThreshold) {
      printf("max_num_reorder_frames_threshold: %" PRId64 "\n", max_num_reorder_frames_threshold);
    }
    ZX_ASSERT(max_num_reorder_frames_threshold >= 0);

    if (print_fps != kDefaultPrintFps) {
      printf("print_fps: %u\n", print_fps);
      if (print_fps && !skip_formatting_output_pixels) {
        printf("Consider also setting skip_formatting_output_pixels");
      }
    }

    if (print_fps_modulus != kDefaultPrintFpsModulus) {
      printf("print_fps_modulus: %" PRIu64 "\n", print_fps_modulus);
    }
    ZX_ASSERT(print_fps_modulus != 0);

    if (per_frame_debug_output != kDefaultPerFrameDebugOutput) {
      printf("per_frame_debug_output: %u\n", per_frame_debug_output);
    }

    if (require_sw != kDefaultRequireSw) {
      printf("require_sw: %u\n", require_sw);
    }

    if (per_frame_golden_sha256 != kDefaultPerFrameGoldenSha256) {
      uint32_t count = 0;
      while (per_frame_golden_sha256[count]) {
        ++count;
      }
      printf("per_frame_golden_sha256 provided - count: %u\n", count);
    }

    if (compare_to_sw_decode != kDefaultCompareToSwDecode) {
      printf("compare_to_sw_decode: %u\n", compare_to_sw_decode);
    }

    if (frame_to_compare != kDefaultFrameToCompare) {
      printf("frame_to_compare set\n");
      // avoid recursion beyond 2
      ZX_ASSERT(!compare_to_sw_decode);
    }

    if (frame_num_gaps != kDefaultFrameNumGaps) {
      printf("frame_num_gaps: %u\n", frame_num_gaps);
    }

    if (min_expected_output_frame_count != kDefaultMinExpectedOutputFrameCount) {
      printf("min_expected_ouput_frame_count: %d\n", min_expected_output_frame_count);
    }

    if (golden_sha256 != kDefaultGoldenSha256) {
      printf("golden_sha256: %s\n", golden_sha256);
    }

    if (skip_formatting_output_pixels != kDefaultSkipFormattingOutputPixels) {
      printf("skip_formatting_output_pixels: %u\n", skip_formatting_output_pixels);
      ZX_ASSERT(skip_formatting_output_pixels);
      ZX_ASSERT(!golden_sha256 && !per_frame_golden_sha256);
    }

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
  // The hash only pays attention to the frames from streams whose stream_lifetime_ordinal %
  // keep_stream_modulo == 0.
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

  // Reset sha256 context each iteration.  This allows looping faster to get a flake to repro more
  // often, and avoids the hash being dependent on loop_stream_count.
  static constexpr bool kDefaultResetHashEachIteration = false;
  bool reset_hash_each_iteration = kDefaultResetHashEachIteration;

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

  // If frames are out of order by more than this much, fail the test (by timing out).
  //
  // We intentionally use uint32_t max not int64_t max.
  static constexpr int64_t kDefaultMaxNumReorderFramesThreshold =
      std::numeric_limits<uint32_t>::max();
  int64_t max_num_reorder_frames_threshold = kDefaultMaxNumReorderFramesThreshold;

  // If true, print the frames-per-second each print_fps_modulus frames.
  static constexpr bool kDefaultPrintFps = false;
  bool print_fps = kDefaultPrintFps;

  // If print_fps is true, print the frames-per-second each print_fps_modulus frames.
  static constexpr uint64_t kDefaultPrintFpsModulus = 1;
  uint64_t print_fps_modulus = kDefaultPrintFpsModulus;

  static constexpr bool kDefaultPerFrameDebugOutput = true;
  bool per_frame_debug_output = kDefaultPerFrameDebugOutput;

  // Require SW decode.
  static constexpr bool kDefaultRequireSw = false;
  bool require_sw = kDefaultRequireSw;

  // Must be either nullptr, or point to a nullptr-terminated array.
  static constexpr char** kDefaultPerFrameGoldenSha256 = nullptr;
  const char** per_frame_golden_sha256 = nullptr;

  // If true, a failure to match per_frame_golden_sha256 will decode up to the mis-matching frame,
  // and then compare that frame pixel-by-pixel, with stderr output indicating the diff in Y, U, and
  // V.  The SW decode for this purpose only occurs if a per_frame_golden_sha256 mis-match occurs
  // first.
  static constexpr bool kDefaultCompareToSwDecode = true;
  bool compare_to_sw_decode = kDefaultCompareToSwDecode;

  // So far, this is only used recursively to compare a HW-decoded frame to a SW-decoded frame.
  //
  // This is the "actual" HW-decoded frame to compare to the corresponding "expected" SW-decoded
  // frame.
  static constexpr FrameToCompare* kDefaultFrameToCompare = nullptr;
  FrameToCompare* frame_to_compare = kDefaultFrameToCompare;

  // Remove some of the frames, to force frame_num gap handling to run.  Do this for lots of frames
  // to check if leaks happen.  We typically don't care what the golden_sha256 is in this case, nor
  // do we expect that the hash would necessarily be consistent from decoder to decoder, as we don't
  // require decoders to handle frame_num gaps in any particular way.  We test that a decoder
  // doesn't get stuck or crash.  At least for now, we test that a decoder does not indicate
  // failure.  The first missing frame_num will be the frame_num of the second picture (ordinal 1,
  // cardinal 2, regardless of what the frame_num values are).  For now this only works with streams
  // that have 1 slice per frame, as it doesn't actually parse the slices for the frame_num or first
  // macroblock number.  But the intent is to skip all frame_num(s) of a picture, not skip slices
  // within a picture (which can be a separate thing).
  static constexpr bool kDefaultFrameNumGaps = false;
  bool frame_num_gaps = kDefaultFrameNumGaps;

  // When using frame_num_gaps true, we can expect a minimum number of output frames, to validate
  // that the decoder outputs at least some frames after the first gap.  We don't require a
  // specific number of frames however, since handling strategies can differ.  If the test stream
  // only contains 1 IDR frame, then this verifies that the decoder doesn't require a new IDR frame
  // to output pictures (which is desirable in that it provides slightly more visual motion
  // continuity, but _will_ result in output pictures that are partly or fully corrupted visually.)
  //
  // None of this frame_num gap stuff is intended to condone input streams with corrupted/missing
  // input data.  Decoders are not required to handle general corrupted/missing data, other than
  // not crashing and not getting stuck.  It's fine if a decoder just indicates stream or codec
  // failure on corrupted/missing input data other than frame_num gaps where exactly entire frames
  // are missing.
  static constexpr int32_t kDefaultMinExpectedOutputFrameCount = -1;
  int32_t min_expected_output_frame_count = kDefaultMinExpectedOutputFrameCount;

  // If non-nullptr, the expected sha256 hash of all the output frame data in I420 format with
  // stride == width.
  static constexpr char* kDefaultGoldenSha256 = nullptr;
  const char* golden_sha256 = kDefaultGoldenSha256;

  static constexpr bool kDefaultSkipFormattingOutputPixels = false;
  bool skip_formatting_output_pixels = kDefaultSkipFormattingOutputPixels;

 private:
  // Client code should not exploit knowledge of this value, and should not directly initialize or
  // directly set magic_validated_ to any value.
  static constexpr uint64_t kPrivateMagicValidated = 0xC001DECAFC0DE;
};

// This represents the "actual" frame (not the "expected" frame).
struct FrameToCompare {
  // I420 format only, for now.

  // data must point at a complete frame, with at least width * height * 3 / 2 bytes
  uint8_t* data;

  // Which "expected" frame ordinal needs to be compared to this "actual" frame.
  uint32_t ordinal;

  // All the pixels width * height Y and width / 2 * height / 2 UV will be compared.
  // The stride == width.
  uint32_t width;
  uint32_t height;
};

struct UseVideoDecoderParams {
  // the loop created and run/started by main().  The codec_factory is
  //     and sysmem are bound to fidl_loop->dispatcher().
  async::Loop* fidl_loop{};
  // the thread on which fidl_loop activity runs.
  thrd_t fidl_thread{};
  // codec_factory to take ownership of, use, and close by the
  //     time the function returns.
  fuchsia::mediacodec::CodecFactoryHandle codec_factory;
  fuchsia::sysmem::AllocatorHandle sysmem;
  InStreamPeeker* in_stream = nullptr;
  InputCopier* input_copier = nullptr;
  uint64_t min_output_buffer_size = 0;
  uint32_t min_output_buffer_count = 0;
  bool is_secure_output = false;
  bool is_secure_input = false;
  bool lax_mode = false;
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
