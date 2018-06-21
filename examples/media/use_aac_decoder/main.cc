// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include "use_aac_decoder.h"

#include <lib/app/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>

void usage(const char* prog_name) {
  printf("usage: %s <input_adts_file> [<output_wav_file>]\n", prog_name);
}

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 3) {
    usage(argv[0]);
    return -1;
  }

  async::Loop main_loop(&kAsyncLoopConfigMakeDefault);
  main_loop.StartThread("FIDL_thread");

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context =
      fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::mediacodec::CodecFactoryPtr codec_factory =
      startup_context
          ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>();

  const char* input_adts_file = argv[1];
  const char* output_wav_file = nullptr;
  if (argc == 3) {
    output_wav_file = argv[2];
  }

  uint8_t md[SHA256_DIGEST_LENGTH];
  use_aac_decoder(std::move(codec_factory), input_adts_file, output_wav_file,
                  md);
  printf(
      "The sha256 of the output audio data (including audio data format "
      "parameters) is:\n");
  for (uint8_t byte : md) {
    printf("%02x", byte);
  }
  printf("\n");

  main_loop.Quit();
  main_loop.JoinThreads();
  startup_context.reset();
  main_loop.Shutdown();

  return 0;
}
