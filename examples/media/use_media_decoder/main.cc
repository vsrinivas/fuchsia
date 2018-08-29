// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include "frame_sink.h"
#include "use_aac_decoder.h"
#include "use_h264_decoder.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>

#include <thread>

void usage(const char* prog_name) {
  printf("usage: %s (--aac_adts|--h264) <input_file> [<output_file>]\n",
         prog_name);
}

int main(int argc, char* argv[]) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }
  if (command_line.positional_args().size() < 1 ||
      command_line.positional_args().size() > 2) {
    usage(command_line.argv0().c_str());
    return -1;
  }

  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([] {
    // TODO(dustingreen): get and print CodecFactory channel epitaph once that's
    // possible.
    FXL_LOG(ERROR) << "codec_factory failed - unexpected";
  });

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();
  startup_context
      ->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>(
          codec_factory.NewRequest());

  std::string input_file = command_line.positional_args()[0];
  std::string output_file;
  if (command_line.positional_args().size() >= 2) {
    output_file = command_line.positional_args()[1];
  }

  // In case of --h264 and --imagepipe, this will be non-nullptr:
  std::unique_ptr<FrameSink> frame_sink;

  uint8_t md[SHA256_DIGEST_LENGTH];

  // We set up a closure here just to avoid forcing the two decoder types to
  // take the same parameters, but still be able to share the
  // drive_decoder_thread code below.
  fit::closure use_decoder;
  if (command_line.HasOption("aac_adts")) {
    use_decoder = [&main_loop, codec_factory = std::move(codec_factory),
                   input_file, output_file, &md]() mutable {
      use_aac_decoder(&main_loop, std::move(codec_factory), input_file,
                      output_file, md);
    };
  } else if (command_line.HasOption("h264")) {
    bool use_imagepipe = command_line.HasOption("imagepipe");

    if (use_imagepipe) {
      // We must do this part of setup on the main thread, not in use_decoder
      // which runs on drive_decoder_thread.  This is because we want the
      // FrameSink (or rather, code it uses) to bind to loop (whether explicitly
      // or implicitly), and we want that setup/binding to occur on the same
      // thread as runs that loop (the current thread), as that's a typical
      // assumption of setup/binding code.
      frame_sink = FrameSink::Create(startup_context.get(), &main_loop);
    }

    use_decoder = [&main_loop, codec_factory = std::move(codec_factory),
                   input_file, output_file, &md,
                   frame_sink = frame_sink.get()]() mutable {
      use_h264_decoder(&main_loop, std::move(codec_factory), input_file,
                       output_file, md, nullptr, frame_sink);
    };
  } else {
    usage(command_line.argv0().c_str());
    return -1;
  }

  auto drive_decoder_thread = std::make_unique<std::thread>(
      [use_decoder = std::move(use_decoder), &main_loop] {
        use_decoder();
        main_loop.Quit();
      });

  main_loop.Run();

  drive_decoder_thread->join();

  printf(
      "The sha256 of the output data (including data format "
      "parameters) is:\n");
  for (uint8_t byte : md) {
    printf("%02x", byte);
  }
  printf("\n");

  // ~frame_sink
  // ~main_loop
  return 0;
}
