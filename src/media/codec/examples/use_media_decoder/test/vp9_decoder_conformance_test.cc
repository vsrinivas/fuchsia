// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This manual test is a basic integration test of the codec_factory +
// amlogic_video_decoder driver.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <set>

#include <openssl/md5.h>

#include "../in_stream_file.h"
#include "../in_stream_http.h"
#include "../in_stream_peeker.h"
#include "../use_video_decoder.h"
#include "../util.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "use_video_decoder_test.h"

namespace {

// The actual contents of these files will vary per test package.  See files
// under garnet/test_data/media/third_party/webm_vp9_conformance_streams.
constexpr char kInputFilePath[] = "/pkg/data/vp9.ivf";
constexpr char kI420Md5FilePath[] = "/pkg/data/vp9.md5";

// VP9 doesn't really need peeking (at least for now), but InStreamPeeker
// requires max_peek_bytes be at least 1, and we read everything via an
// InStreamPeeker for consistency with h264 which does need a lot more peeking
// to scan for start codes.
constexpr uint32_t kMaxPeekBytes = 1;

constexpr uint32_t kMd5CharCount = MD5_DIGEST_LENGTH * 2;

void usage(const char* prog_name) { printf("usage: %s [--url=<url>]\n", prog_name); }

std::string md5_string_non_destructive(MD5_CTX* md5_ctx) {
  uint8_t md5_digest[MD5_DIGEST_LENGTH];
  // intentional struct copy
  MD5_CTX context_copy = *md5_ctx;

  ZX_ASSERT(MD5_Final(md5_digest, &context_copy));

  char actual_md5_chars[kMd5CharCount + 1];
  char* actual_md5_ptr = actual_md5_chars;
  for (uint8_t byte : md5_digest) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_md5_ptr += snprintf(actual_md5_ptr, 3, "%02x", byte);
  }
  FXL_CHECK(actual_md5_ptr == actual_md5_chars + kMd5CharCount);

  return std::string(actual_md5_chars, kMd5CharCount);
}

}  // namespace

int main(int argc, char* argv[]) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }
  if (command_line.positional_args().size() != 0) {
    usage(command_line.argv0().c_str());
    exit(-1);
  }
  std::string url;
  bool is_url = command_line.GetOptionValue("url", &url);

  async::Loop fidl_loop(&kAsyncLoopConfigAttachToCurrentThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == fidl_loop.StartThread("FIDL_thread", &fidl_thread));
  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();

  std::unique_ptr<InStream> raw_stream;
  if (is_url) {
    raw_stream =
        std::make_unique<InStreamHttp>(&fidl_loop, fidl_thread, component_context.get(), url);
  } else {
    raw_stream = std::make_unique<InStreamFile>(&fidl_loop, fidl_thread, component_context.get(),
                                                kInputFilePath);
  }
  auto in_stream_peeker = std::make_unique<InStreamPeeker>(
      &fidl_loop, fidl_thread, component_context.get(), std::move(raw_stream), kMaxPeekBytes);

  std::string expected_md5_string;
  if (!is_url) {
    size_t md5_size = 0;
    std::unique_ptr<uint8_t[]> md5_data = read_whole_file(kI420Md5FilePath, &md5_size);
    ZX_DEBUG_ASSERT(md5_size >= kMd5CharCount);
    expected_md5_string = std::string(reinterpret_cast<char*>(md5_data.get()), kMd5CharCount);
  }

  MD5_CTX md5_ctx{};
  ZX_ASSERT(MD5_Init(&md5_ctx));

  uint32_t frame_counter = 0;
  EmitFrame emit_frame = [&md5_ctx, &frame_counter](
                             uint8_t* i420_base, uint32_t width, uint32_t height, uint32_t stride,
                             bool has_timestamp_ish, uint64_t timestamp_ish) {
    // The handling for odd height has _not_ successfully matched an MD5 from
    // the VP9 decoder conformance spreadsheet yet.
    //
    // The handling for odd width _has_ successfully matched MD5s from the VP9
    // decoder conformance spreadsheet (when height is even).
    //
    // Odd stride is not handled, becuase we don't know what that would mean for
    // stride of u and v, and we don't have any examples so far where handling
    // odd stride is necessary.
    ZX_ASSERT(stride % 2 == 0);
    ZX_DEBUG_ASSERT(width <= stride);
    uint32_t half_width = (width + 1) / 2;
    uint32_t half_height = (height + 1) / 2;
    ZX_DEBUG_ASSERT(half_width <= stride / 2);
    uint8_t* iter = i420_base;
    // Y
    for (uint32_t i = 0; i < height; ++i) {
      ZX_ASSERT(MD5_Update(&md5_ctx, iter, width));
      iter += stride;
    }
    // U
    for (uint32_t i = 0; i < half_height; ++i) {
      ZX_ASSERT(MD5_Update(&md5_ctx, iter, half_width));
      iter += stride / 2;
    }
    // V
    for (uint32_t i = 0; i < half_height; ++i) {
      ZX_ASSERT(MD5_Update(&md5_ctx, iter, half_width));
      iter += stride / 2;
    }
    std::string md5_so_far = md5_string_non_destructive(&md5_ctx);

    printf("MD5_Update - frame_counter: %u width: %u height: %u md5_so_far: %s\n", frame_counter,
           width, height, md5_so_far.c_str());
    fflush(stdout);
    frame_counter++;
  };

  // Forcing buffers to be larger up front should allow dynamic frame dimension
  // changes to be seamless, even if we find a stream that starts with smaller
  // dimensions.  So far, all the streams that change resolution seem to
  // start with larger dimensions however, so this can be 0 for now.
  uint64_t min_output_buffer_size = 0;

  if (!decode_video_stream_test(&fidl_loop, fidl_thread, component_context.get(),
                                in_stream_peeker.get(), use_vp9_decoder, min_output_buffer_size,
                                /*min_output_buffer_count=*/0,
                                /*is_secure_output=*/false, /*is_secure_input=*/false,
                                std::move(emit_frame))) {
    FXL_LOG(FATAL) << "decode_video_stream_test() failed";
  }

  std::string actual_md5 = md5_string_non_destructive(&md5_ctx);
  printf("Done decoding - computed md5 is: %s\n", actual_md5.c_str());
  if (!is_url) {
    if (strcmp(actual_md5.c_str(), expected_md5_string.c_str())) {
      printf("The md5 doesn't match - expected: %s actual: %s\n", expected_md5_string.c_str(),
             actual_md5.c_str());
      exit(-1);
    }
    printf("The computed md5 matches.  Yay!\nPASS\n");
  } else {
    printf("The return code of 0 does _not_ imply the md5 is correct\n");
  }

  return 0;
}
