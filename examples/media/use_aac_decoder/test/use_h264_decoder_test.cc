// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <stdio.h>
#include <stdlib.h>

#include "../use_h264_decoder.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include "lib/fxl/logging.h"

#include <set>

namespace {

constexpr char kInputFilePath[] =
    "/pkgfs/packages/media_examples_manual_tests/0/data/media_test_data/"
    "bear.h264";

constexpr char kGoldenSha256[SHA256_DIGEST_LENGTH * 2 + 1] =
    "212aad741a1f2f560753f2b2731afe483e975ec5891dcd20cf93fe1c57b1091a";

}  // namespace

int main(int argc, char* argv[]) {
  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);
  main_loop.StartThread("FIDL_thread");

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();
  fuchsia::mediacodec::CodecFactoryPtr codec_factory =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>();

  printf("The test file is: %s\n", kInputFilePath);
  printf("The expected sha256 is: %s\n", kGoldenSha256);
  printf("Decoding test file and computing sha256...\n");

  uint8_t md[SHA256_DIGEST_LENGTH];
  std::vector<std::pair<bool, uint64_t>> timestamps;
  use_h264_decoder(std::move(codec_factory), kInputFilePath, "", md,
                   &timestamps);

  std::set<uint64_t> expected_timestamps;
  for (uint64_t i = 0; i < 30; i++) {
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
          "degree of re-ordering - output frame ordinal: %lu timestamp_ish: "
          "%lu\n",
          i, timestamps[i].second);
      exit(-1);
    }
    if (expected_timestamps.find(timestamps[i].second) ==
        expected_timestamps.end()) {
      printf(
          "A frame had timestamp_ish not in the expected set (or duplicated) - "
          "output frame ordinal: %lu timestamp_ish: 0x%lx\n",
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
  if (strcmp(actual_sha256, kGoldenSha256)) {
    printf("The sha256 doesn't match - expected: %s actual: %s\n",
           kGoldenSha256, actual_sha256);
    exit(-1);
  }
  printf("The computed sha256 matches kGoldenSha256.  Yay!\nPASS\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  startup_context.reset();
  main_loop.Shutdown();

  return 0;
}
