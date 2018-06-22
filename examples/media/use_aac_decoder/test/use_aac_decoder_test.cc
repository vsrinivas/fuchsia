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

#include <stdio.h>
#include <stdlib.h>

#include "../use_aac_decoder.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/util.h>
#include "garnet/bin/appmgr/appmgr.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fxl/logging.h"

namespace {

// In case use_aac_decoder seems broken, the audio file has a voice saying
// this in it (mono 16 bit 44.1 kHz):
//
// "Copyright 2018 The Fuchsia Authors. All rights reserved. Use of this audio
// file is governed by a BSD-style license that can be found in the LICENSE
// file."
constexpr char kInputFilePath[] =
    "/system/data/media_test_data/test_audio.adts";

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

}  // namespace

int main(int argc, char* argv[]) {
  // Run an appmgr instance locally, which will start a sysmgr process as a
  // separate process.  That sysmgr process will start a codec_factory process
  // when a request for pa_directory/svc/fuchsia.mediacodec.CodecFactory
  // arrives.
  async::Loop main_loop(&kAsyncLoopConfigMakeDefault);
  zx::channel appmgr_pa_directory_client, appmgr_pa_directory_server;
  zx_status_t zx_result = zx::channel::create(0, &appmgr_pa_directory_client,
                                              &appmgr_pa_directory_server);
  if (zx_result != ZX_OK) {
    printf("zx::channel::create() failed\n");
    exit(-1);
  }
  fidl::VectorPtr<fidl::StringPtr> sysmgr_args;
  sysmgr_args.push_back(
      "--config={\"services\": { \"fuchsia.mediacodec.CodecFactory\": "
      "\"codec_factory\" } }");
  sysmgr_args.push_back("--test");
  std::unique_ptr<component::Appmgr> appmgr =
      std::make_unique<component::Appmgr>(
          main_loop.async(),
          component::AppmgrArgs{
              .pa_directory_request = appmgr_pa_directory_server.release(),
              .sysmgr_url = "sysmgr",
              .sysmgr_args = std::move(sysmgr_args),
              .run_virtual_console = false,
              .retry_sysmgr_crash = false,
          });
  main_loop.StartThread("main_loop");
  zx::channel appmgr_svc_dir_client, appmgr_svc_dir_server;
  zx_result =
      zx::channel::create(0, &appmgr_svc_dir_client, &appmgr_svc_dir_server);
  if (zx_result != ZX_OK) {
    printf("zx::channel::create() failed (2)\n");
    exit(-1);
  }
  zx_result = fdio_service_connect_at(appmgr_pa_directory_client.get(), "svc",
                                      appmgr_svc_dir_server.release());
  if (zx_result != ZX_OK) {
    printf("fdio_service_connect_at() failed\n");
    exit(-1);
  }

  // The svcmgr started by appmgr will handle requests for
  // pa_directory/svc/fuchsia.mediacodec.CodecFactory by creating a
  // codec_factory process, but that doesn't mean that code running in this
  // integration test can call
  // startup_context->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>();
  // Instead, we connect to the CodecFactory here, and pass that into
  // use_aac_decoder().

  // This gets sysmgr code to start CodecFactory the same way it would in a real
  // system, but this doesn't let this test ask for the CodecFactory based on
  // the test's process-local /svc directory.
  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  zx_result = fdio_service_connect_at(
      appmgr_svc_dir_client.get(), ::fuchsia::mediacodec::CodecFactory::Name_,
      codec_factory.NewRequest(main_loop.async()).TakeChannel().release());
  if (zx_result != ZX_OK) {
    printf("fdio_service_connect_at() failed (2)\n");
    exit(-1);
  }

  printf("The test file is: %s\n", kInputFilePath);
  printf("The expected sha256 on x64 is: %s\n", kGoldenSha256_x64);
  printf("The expected sha256 on arm64 is: %s\n", kGoldenSha256_arm64);
  printf("Decoding test file and computing sha256...\n");

  uint8_t md[SHA256_DIGEST_LENGTH];
  use_aac_decoder(std::move(codec_factory), kInputFilePath, "", md);

  char actual_sha256[SHA256_DIGEST_LENGTH * 2 + 1];
  char* actual_sha256_ptr = actual_sha256;
  for (uint8_t byte : md) {
    // Writes the terminating 0 each time, returns 2 each time.
    actual_sha256_ptr += snprintf(actual_sha256_ptr, 3, "%02x", byte);
  }
  assert(actual_sha256_ptr == actual_sha256 + SHA256_DIGEST_LENGTH * 2);
  printf("Done decoding - computed sha256 is: %s\n", actual_sha256);
  if (strcmp(actual_sha256, kGoldenSha256_x64) &&
      strcmp(actual_sha256, kGoldenSha256_arm64)) {
    printf(
        "The sha256 doesn't match - expected: %s (x64) or %s (arm64) actual: "
        "%s\n",
        kGoldenSha256_x64, kGoldenSha256_arm64, actual_sha256);
    exit(-1);
  }
  printf(
      "The computed sha256 matches kGoldenSha256_x64 or kGoldenSha256_arm64.  "
      "Yay!\nPASS\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  appmgr.reset();
  main_loop.Shutdown();

  return 0;
}
