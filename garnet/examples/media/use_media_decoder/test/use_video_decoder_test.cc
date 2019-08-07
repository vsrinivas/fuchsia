// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include "use_video_decoder_test.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/media/codec_impl/fourcc.h>
#include <src/lib/fxl/logging.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include "../use_video_decoder.h"
#include "../util.h"
#include "../in_stream_file.h"
#include "../in_stream_peeker.h"

namespace {

// 8MiB max peek is essentially for h264 streams.  VP9 streams don't need to
// scan for start codes so won't peek anywhere near this much.
constexpr uint32_t kMaxPeekBytes = 8 * 1024 * 1024;

}

int use_video_decoder_test(
    std::string input_file_path, int expected_frame_count,
    UseVideoDecoderFunction use_video_decoder,
    std::string golden_sha256) {
  async::Loop fidl_loop(&kAsyncLoopConfigAttachToThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == fidl_loop.StartThread("FIDL_thread", &fidl_thread));
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  printf("Decoding test file %s\n", input_file_path.c_str());

  auto in_stream_file = std::make_unique<InStreamFile>(
    &fidl_loop,
    fidl_thread,
    startup_context.get(),
    input_file_path);
  auto in_stream_peeker = std::make_unique<InStreamPeeker>(
    &fidl_loop,
    fidl_thread,
    startup_context.get(),
    std::move(in_stream_file),
    kMaxPeekBytes);

  std::vector<std::pair<bool, uint64_t>> timestamps;
  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);

  EmitFrame emit_frame = [&sha256_ctx, &timestamps](uint8_t* i420_data, uint32_t width, uint32_t height, uint32_t stride, bool has_timestamp_ish, uint64_t timestamp_ish){
    ZX_ASSERT_MSG(width % 2 == 0, "odd width not yet handled");
    ZX_ASSERT_MSG(width == stride, "stride != width not yet handled");
    timestamps.push_back({has_timestamp_ish, timestamp_ish});
    SHA256_Update(&sha256_ctx, i420_data, width * height * 3 / 2);
  };

  if (!decode_video_stream_test(&fidl_loop, fidl_thread, startup_context.get(), in_stream_peeker.get(), use_video_decoder, 0, std::move(emit_frame))) {
    printf("decode_video_stream_test() failed.\n");
    return -1;
  }

  const int frame_count = expected_frame_count != -1 ? expected_frame_count : timestamps.size();
  std::set<uint64_t> expected_timestamps;
  for (int i = 0; i < frame_count; i++) {
    expected_timestamps.insert(i);
  }
  for (size_t i = 0; i < timestamps.size(); i++) {
    if (!timestamps[i].first) {
      printf("A frame had !has_timstamp_ish - frame_index: %lu\n", i);
      exit(-1);
    }
    int64_t output_frame_index = i;
    int64_t timestamp_ish = timestamps[i].second;
    if (timestamp_ish < output_frame_index - 1 && timestamp_ish > output_frame_index + 1) {
      printf(
          "A frame had output timestamp_ish out of order beyond expected "
          "degree of re-ordering - frame_index: %lu timestamp_ish: "
          "%lu\n",
          i, timestamps[i].second);
      exit(-1);
    }
    if (expected_timestamps.find(timestamps[i].second) == expected_timestamps.end()) {
      printf(
          "A frame had timestamp_ish not in the expected set (or duplicated) - "
          "frame_index: %lu timestamp_ish: %lu\n",
          i, timestamps[i].second);
      exit(-1);
    }
    expected_timestamps.erase(timestamps[i].second);
  }
  if (!expected_timestamps.empty()) {
    printf("not all expected_timestamps seen\n");
    for (uint64_t timestamp : expected_timestamps) {
      printf("missing timestamp: %lx\n", timestamp);
    }
    exit(-1);
  }

  uint8_t md[SHA256_DIGEST_LENGTH] = {};
  ZX_ASSERT(SHA256_Final(md, &sha256_ctx));
  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint8_t byte : md) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", byte);
  }
  FXL_CHECK(actual_sha256_ptr == actual_sha256 + SHA256_DIGEST_LENGTH * 2);
  printf("Done decoding - computed sha256 is: %s\n", actual_sha256);
  if (strcmp(actual_sha256, golden_sha256.c_str())) {
    printf("The sha256 doesn't match - expected: %s actual: %s\n",
          golden_sha256.c_str(), actual_sha256);
    exit(-1);
  }
  printf("The computed sha256 matches golden sha256.  Yay!\nPASS\n");

  fidl_loop.Quit();
  fidl_loop.JoinThreads();
  startup_context.reset();
  fidl_loop.Shutdown();

  return 0;
}

bool decode_video_stream_test(
    async::Loop* fidl_loop,
    thrd_t fidl_thread,
    component::StartupContext* startup_context,
    InStreamPeeker* in_stream_peeker,
    UseVideoDecoderFunction use_video_decoder,
    uint64_t min_output_buffer_size,
    EmitFrame emit_frame) {
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([](zx_status_t status) {
    FXL_PLOG(FATAL, status) << "codec_factory failed - unexpected";
  });
  startup_context
      ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>(
          codec_factory.NewRequest());
  fuchsia::sysmem::AllocatorPtr sysmem;
  sysmem.set_error_handler([](zx_status_t status){
    FXL_PLOG(FATAL, status) << "sysmem failed - unexpected";
  });
  startup_context->ConnectToEnvironmentService<fuchsia::sysmem::Allocator>(
      sysmem.NewRequest());

  use_video_decoder(fidl_loop, fidl_thread, std::move(codec_factory), std::move(sysmem), in_stream_peeker, min_output_buffer_size, nullptr, std::move(emit_frame));

  return true;
}
