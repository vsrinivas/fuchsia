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

int use_video_decoder_test(
    const char* input_file_path, int frame_count,
    UseVideoDecoderFunction use_video_decoder,
    const std::map<uint32_t, const char*>& golden_sha256s) {
  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);
  main_loop.StartThread("FIDL_thread");

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([](zx_status_t status) {
    // TODO(dustingreen): get and print CodecFactory channel epitaph once that's
    // possible.
    FXL_LOG(FATAL) << "codec_factory failed - unexpected";
  });

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();
  startup_context
      ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>(
          codec_factory.NewRequest());

  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
  startup_context->ConnectToEnvironmentService<fuchsia::sysmem::Allocator>(
      sysmem.NewRequest());

  printf("The test file is: %s\n", input_file_path);
  printf("Decoding test file and computing sha256...\n");

  uint8_t md[SHA256_DIGEST_LENGTH];
  std::vector<std::pair<bool, uint64_t>> timestamps;
  uint32_t fourcc;
  use_video_decoder(&main_loop, std::move(codec_factory), std::move(sysmem),
                    input_file_path, "", md, &timestamps, &fourcc, nullptr);

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
    if (timestamp_ish < output_frame_index - 1 &&
        timestamp_ish > output_frame_index + 1) {
      printf(
          "A frame had output timestamp_ish out of order beyond expected "
          "degree of re-ordering - frame_index: %lu timestamp_ish: "
          "%lu\n",
          i, timestamps[i].second);
      exit(-1);
    }
    if (expected_timestamps.find(timestamps[i].second) ==
        expected_timestamps.end()) {
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

  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint8_t byte : md) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", byte);
  }
  FXL_CHECK(actual_sha256_ptr == actual_sha256 + SHA256_DIGEST_LENGTH * 2);
  printf("Done decoding - computed sha256 is: %s\n", actual_sha256);
  if (strcmp(actual_sha256, golden_sha256s.at(fourcc))) {
    printf("The sha256 doesn't match - expected: %s actual: %s\n",
           golden_sha256s.at(fourcc), actual_sha256);
    exit(-1);
  }
  printf("The computed sha256 matches kGoldenSha256.  Yay!\nPASS\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  startup_context.reset();
  main_loop.Shutdown();

  return 0;
}
