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
#include <map>

#include "../use_video_decoder.h"
#include "../util.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/logging.h>
#include <lib/media/codec_impl/fourcc.h>

#include <set>

namespace {

constexpr char kInputFilePath[] = "/pkg/data/bear.h264";

const std::map<uint32_t, const char*> GoldenSha256s = {
    {make_fourcc('Y', 'V', '1', '2'),
     "39e861466dede78e5be008f85dba53efcee23b7a064170e4c00361383e67690d"},
     // YV12 without SHA256_Update_VideoParameters():
     // f3116ef8cf0f69c3d9316246a3896f96684f513ce9664b9b55e195c964cc64a0
    {make_fourcc('N', 'V', '1', '2'),
     "2ab4b1f47636ac367b5cc0da2bf8d901a9e2b5db40126b50f5f75ee5b3b8c8df"}};
     // NV12 without SHA256_Update_VideoParameters():
     // 84ae3e279d8b85d3a3b10c06489d9ffb0a968d99baa498d20f28788c0090c1d5
}  // namespace

int main(int argc, char* argv[]) {
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

  printf("The test file is: %s\n", kInputFilePath);
  printf("Decoding test file and computing sha256...\n");

  uint8_t md[SHA256_DIGEST_LENGTH];
  std::vector<std::pair<bool, uint64_t>> timestamps;
  uint32_t fourcc;
  use_h264_decoder(&main_loop, std::move(codec_factory), std::move(sysmem), kInputFilePath, "", md,
                   &timestamps, &fourcc, nullptr);

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
  if (strcmp(actual_sha256, GoldenSha256s.at(fourcc))) {
    printf("The sha256 doesn't match - expected: %s actual: %s\n",
           GoldenSha256s.at(fourcc), actual_sha256);
    exit(-1);
  }
  printf("The computed sha256 matches kGoldenSha256.  Yay!\nPASS\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  startup_context.reset();
  main_loop.Shutdown();

  return 0;
}
