// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include "use_video_decoder_test.h"

#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include "../in_stream_buffer.h"
#include "../in_stream_file.h"
#include "../in_stream_peeker.h"
#include "../input_copier.h"
#include "../use_video_decoder.h"
#include "../util.h"
#include "openssl/base.h"

namespace {

// 8MiB max peek is essentially for h264 streams.  VP9 streams don't need to
// scan for start codes so won't peek anywhere near this much.
constexpr uint32_t kMaxPeekBytes = 8 * 1024 * 1024;
constexpr uint64_t kMaxBufferBytes = 8 * 1024 * 1024;

std::mutex tags_lock;

std::string GetSha256SoFar(const SHA256_CTX* sha256_ctx) {
  uint8_t md[SHA256_DIGEST_LENGTH] = {};
  // struct copy so caller can keep hashing more data into sha256_ctx
  SHA256_CTX sha256_ctx_copy = *sha256_ctx;
  ZX_ASSERT(SHA256_Final(md, &sha256_ctx_copy));
  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint8_t byte : md) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", byte);
  }
  FX_CHECK(actual_sha256_ptr == actual_sha256 + SHA256_DIGEST_LENGTH * 2);
  return std::string(actual_sha256, SHA256_DIGEST_LENGTH * 2);
}

}  // namespace

int use_video_decoder_test(std::string input_file_path, int expected_frame_count,
                           UseVideoDecoderFunction use_video_decoder, bool is_secure_output,
                           bool is_secure_input, uint32_t min_output_buffer_count,
                           std::string golden_sha256,
                           const UseVideoDecoderTestParams* test_params) {
  const UseVideoDecoderTestParams default_test_params;
  if (!test_params) {
    test_params = &default_test_params;
  }
  test_params->Validate();

  {
    std::lock_guard<std::mutex> lock(tags_lock);
    syslog::SetTags({"use_video_decoder_test"});
  }

  async::Loop fidl_loop(&kAsyncLoopConfigAttachToCurrentThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == fidl_loop.StartThread("FIDL_thread", &fidl_thread));
  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  printf("Decoding test file %s\n", input_file_path.c_str());

  std::unique_ptr<InStream> in_stream_so_far = std::make_unique<InStreamFile>(
      &fidl_loop, fidl_thread, component_context.get(), input_file_path);
  // default 1
  const int64_t loop_stream_count = test_params->loop_stream_count;
  if (loop_stream_count >= 2) {
    std::unique_ptr<InStream> next_in_stream;
    next_in_stream =
        std::make_unique<InStreamBuffer>(&fidl_loop, fidl_thread, component_context.get(),
                                         std::move(in_stream_so_far), kMaxBufferBytes);
    in_stream_so_far = std::move(next_in_stream);
  }
  auto in_stream_peeker = std::make_unique<InStreamPeeker>(
      &fidl_loop, fidl_thread, component_context.get(), std::move(in_stream_so_far), kMaxPeekBytes);

  std::vector<std::pair<bool, uint64_t>> timestamps;
  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);

  uint32_t frame_index = 0;
  bool got_output_data = false;
  // default 2
  const uint64_t keep_stream_modulo = test_params->keep_stream_modulo;
  EmitFrame emit_frame =
      [&sha256_ctx, &timestamps, &frame_index, &got_output_data, keep_stream_modulo, test_params](
          uint64_t stream_lifetime_ordinal, uint8_t* i420_data, uint32_t width, uint32_t height,
          uint32_t stride, bool has_timestamp_ish, uint64_t timestamp_ish) {
        VLOGF("emit_frame stream_lifetime_ordinal: %" PRIu64
              " frame_index: %u has_timestamp_ish: %d timestamp_ish: %" PRId64,
              stream_lifetime_ordinal, frame_index, has_timestamp_ish, timestamp_ish);
        // For debugging a flake:
        if (test_params->loop_stream_count > 1) {
          LOGF("emit_frame stream_lifetime_ordinal: %" PRIu64
               " frame_index: %u has_timestamp_ish: %d timestamp_ish: %" PRId64,
               stream_lifetime_ordinal, frame_index, has_timestamp_ish, timestamp_ish);
        }
        ZX_DEBUG_ASSERT(stream_lifetime_ordinal % 2 == 1);
        ZX_ASSERT_MSG(width % 2 == 0, "odd width not yet handled");
        ZX_ASSERT_MSG(width == stride, "stride != width not yet handled");
        auto increment_frame_index = fit::defer([&frame_index] { frame_index++; });
        // For streams where this isn't true, we don't flush the input EOS, so there's no guarantee
        // how many output frames we'll get.
        if (stream_lifetime_ordinal % keep_stream_modulo != 1) {
          // ~increment_frame_index
          return;
        }
        timestamps.push_back({has_timestamp_ish, timestamp_ish});
        if (i420_data) {
          got_output_data = true;
          SHA256_Update(&sha256_ctx, i420_data, width * height * 3 / 2);
          std::string sha256_so_far = GetSha256SoFar(&sha256_ctx);
          LOGF("frame_index: %u SHA256 so far: %s", frame_index, sha256_so_far.c_str());
        }
        // ~increment_frame_index
      };

  if (!decode_video_stream_test(&fidl_loop, fidl_thread, component_context.get(),
                                in_stream_peeker.get(), use_video_decoder, 0,
                                min_output_buffer_count, is_secure_output, is_secure_input,
                                std::move(emit_frame), test_params)) {
    printf("decode_video_stream_test() failed.\n");
    return -1;
  }

  const int frame_count = expected_frame_count != -1 ? expected_frame_count : timestamps.size();
  std::set<uint64_t> expected_timestamps;

  // default 0
  const int64_t first_expected_output_frame_ordinal =
      test_params->first_expected_output_frame_ordinal;

  for (int i = first_expected_output_frame_ordinal; i < frame_count; i++) {
    expected_timestamps.insert(i);
  }
  for (size_t i = 0; i < timestamps.size(); i++) {
    if (!timestamps[i].first) {
      printf("A frame had !has_timstamp_ish - frame_index: %lu\n", i);
      return -1;
    }
    int64_t output_frame_index = i;
    int64_t timestamp_ish = timestamps[i].second;
    if (timestamp_ish < output_frame_index - 1 && timestamp_ish > output_frame_index + 1) {
      printf(
          "A frame had output timestamp_ish out of order beyond expected "
          "degree of re-ordering - frame_index: %lu timestamp_ish: "
          "%lu\n",
          i, timestamps[i].second);
      return -1;
    }
    if (expected_timestamps.find(timestamps[i].second) == expected_timestamps.end()) {
      printf(
          "A frame had timestamp_ish not in the expected set (or duplicated) - "
          "frame_index: %lu timestamp_ish: %lu\n",
          i, timestamps[i].second);
      return -1;
    }
    expected_timestamps.erase(timestamps[i].second);
  }
  if (!expected_timestamps.empty()) {
    printf("not all expected_timestamps seen\n");
    for (uint64_t timestamp : expected_timestamps) {
      printf("missing timestamp: %lu\n", timestamp);
    }
    return -1;
  }

  if (got_output_data) {
    std::string actual_sha256 = GetSha256SoFar(&sha256_ctx);
    printf("Done decoding - computed sha256 is: %s\n", actual_sha256.c_str());
    if (strcmp(actual_sha256.c_str(), golden_sha256.c_str())) {
      printf("The sha256 doesn't match - expected: %s actual: %s\n", golden_sha256.c_str(),
             actual_sha256.c_str());
      return -1;
    }
    printf("The computed sha256 matches golden sha256.  Yay!\nPASS\n");
  } else if (is_secure_output) {
    printf("Can't check output data sha256 because output is secure.\nPASS.\n");
  } else {
    printf("No output data received");
    return -1;
  }

  fidl_loop.Quit();
  fidl_loop.JoinThreads();
  component_context.reset();
  fidl_loop.Shutdown();

  return 0;
}

bool decode_video_stream_test(async::Loop* fidl_loop, thrd_t fidl_thread,
                              sys::ComponentContext* component_context,
                              InStreamPeeker* in_stream_peeker,
                              UseVideoDecoderFunction use_video_decoder,
                              uint64_t min_output_buffer_size, uint32_t min_output_buffer_count,
                              bool is_secure_output, bool is_secure_input, EmitFrame emit_frame,
                              const UseVideoDecoderTestParams* test_params) {
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler(
      [](zx_status_t status) { FX_PLOGS(FATAL, status) << "codec_factory failed - unexpected"; });
  component_context->svc()->Connect<fuchsia::mediacodec::CodecFactory>(codec_factory.NewRequest());
  fuchsia::sysmem::AllocatorPtr sysmem;
  sysmem.set_error_handler(
      [](zx_status_t status) { FX_PLOGS(FATAL, status) << "sysmem failed - unexpected"; });
  component_context->svc()->Connect<fuchsia::sysmem::Allocator>(sysmem.NewRequest());

  std::unique_ptr<InputCopier> input_copier;
  if (is_secure_input)
    input_copier = InputCopier::Create();

  UseVideoDecoderParams params{.fidl_loop = fidl_loop,
                               .fidl_thread = fidl_thread,
                               .codec_factory = std::move(codec_factory),
                               .sysmem = std::move(sysmem),
                               .in_stream = in_stream_peeker,
                               .input_copier = input_copier.get(),
                               .min_output_buffer_size = min_output_buffer_size,
                               .min_output_buffer_count = min_output_buffer_count,
                               .is_secure_output = is_secure_output,
                               .is_secure_input = is_secure_input,
                               .lax_mode = false,
                               .emit_frame = std::move(emit_frame),
                               .test_params = test_params};
  use_video_decoder(std::move(params));

  return true;
}
