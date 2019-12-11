// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is mainly serving as a basic integration test of the
// codec_factory + codec_runner_sw_omx, and happens to also run the
// use_aac_decoder code.
//
// If this test breaks and it's not immediately obvoius why, please feel free to
// involve dustingreen@ (me) in figuring it out.  It is recognized and
// acknowledged that there is not enough unit test coverage yet.  A main benefit
// of that coverage will be making test failures of this test easier to narrow
// down.

#include "src/media/codec/examples/use_media_decoder/use_aac_decoder.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>
#include <stdlib.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace {

// In case use_aac_decoder seems broken, the audio file has a voice saying
// this in it (mono 16 bit 44.1 kHz):
//
// "Copyright 2018 The Fuchsia Authors. All rights reserved. Use of this audio
// file is governed by a BSD-style license that can be found in the LICENSE
// file."
constexpr char kInputFilePath[] = "/pkg/data/media_test_data/test_audio.adts";

// Both of these outputs sound "correct".  When compared with "cmp -l" (octal
// byte values), most bytes are the same, and those that differ are different by
// 1.  It's not consisent whether the x64 byte or the arm64 byte is larger.
//
// We don't bother detecting which we're running on - we just accept either as
// "correct" for now.
//
// TODO(dustingreen): Diagnose which arm64-optimized code in the AAC decoder is
// causing this, try to determine which is correct, and try to make correct for
// both x64 and arm64.
constexpr char kGoldenSha256_x64[SHA256_DIGEST_LENGTH * 2 + 1] =
    "e1981e8b2db397d7d4ffc6e50f155a397eeedf37afdfcfd4f66b6b077734f39e";
constexpr char kGoldenSha256_arm64[SHA256_DIGEST_LENGTH * 2 + 1] =
    "f0b7fadd99727a57e5529efb9eefd2dc1beee592d87766a5d9a0d9ae5593bb50";

TEST(DecoderTest, AacDecoder) {
  async::Loop main_loop(&kAsyncLoopConfigAttachToCurrentThread);
  main_loop.StartThread("main_loop");
  auto component_context = sys::ComponentContext::Create();
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  component_context->svc()->Connect(codec_factory.NewRequest(main_loop.dispatcher()));

  fidl::InterfaceHandle<fuchsia::sysmem::Allocator> sysmem;
  component_context->svc()->Connect<fuchsia::sysmem::Allocator>(sysmem.NewRequest());

  uint8_t md[SHA256_DIGEST_LENGTH];
  use_aac_decoder(&main_loop, std::move(codec_factory), std::move(sysmem), kInputFilePath, "", md);

  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint8_t byte : md) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", byte);
  }
  EXPECT_EQ(actual_sha256_ptr, actual_sha256 + SHA256_DIGEST_LENGTH * 2);
  EXPECT_TRUE(!strcmp(actual_sha256, kGoldenSha256_x64) ||
              !strcmp(actual_sha256, kGoldenSha256_arm64))
      << "The sha256 doesn't match - expected: " << kGoldenSha256_x64 << " (x64) or "
      << kGoldenSha256_arm64 << " (arm64) actual: " << actual_sha256;

  main_loop.Quit();
  main_loop.JoinThreads();
  main_loop.Shutdown();
}

}  // namespace
