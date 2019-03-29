// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>

#include "use_aac_decoder.h"
#include "use_video_decoder.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <lib/media/test/frame_sink.h>

#include <thread>

void usage(const char* prog_name) {
  printf(
      "usage: %s (--aac_adts|--h264) [--imagepipe [--fps=<double>]] "
      "<input_file> [<output_file>]\n",
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
    exit(-1);
  }

  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler([](zx_status_t status) {
    // TODO(dustingreen): get and print CodecFactory channel epitaph once that's
    // possible.
    FXL_LOG(ERROR) << "codec_factory failed - unexpected; status: " << status;
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

  bool use_imagepipe = command_line.HasOption("imagepipe");

  double frames_per_second = 0.0;
  std::string frames_per_second_string;
  if (command_line.GetOptionValue("fps", &frames_per_second_string)) {
    if (!use_imagepipe) {
      printf("--fps requires --imagepipe\n");
      usage(command_line.argv0().c_str());
      exit(-1);
    }
    const char* str_begin = frames_per_second_string.c_str();
    char* str_end;
    errno = 0;
    frames_per_second = std::strtod(str_begin, &str_end);
    if (str_end == str_begin ||
        (frames_per_second == HUGE_VAL && errno == ERANGE)) {
      printf("fps parse error\n");
      usage(command_line.argv0().c_str());
      exit(-1);
    }
  }

  if (use_imagepipe) {
    // We must do this part of setup on the main thread, not in use_decoder
    // which runs on drive_decoder_thread.  This is because we want the
    // FrameSink (or rather, code it uses) to bind to loop (whether explicitly
    // or implicitly), and we want that setup/binding to occur on the same
    // thread as runs that loop (the current thread), as that's a typical
    // assumption of setup/binding code.
    // TODO(turnage): Rework to catch the first few frames using view connected
    //                callback.
    frame_sink =
        FrameSink::Create(startup_context.get(), &main_loop, frames_per_second,
                          [](FrameSink* _frame_sink) {});
  }
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
    use_decoder = [&main_loop, codec_factory = std::move(codec_factory),
                   input_file, output_file, &md,
                   frame_sink = frame_sink.get()]() mutable {
      use_h264_decoder(&main_loop, std::move(codec_factory), input_file,
                       output_file, md, nullptr, nullptr, frame_sink);
    };
  } else if (command_line.HasOption("vp9")) {
    use_decoder = [&main_loop, codec_factory = std::move(codec_factory),
                   input_file, output_file, &md,
                   frame_sink = frame_sink.get()]() mutable {
      use_vp9_decoder(&main_loop, std::move(codec_factory), input_file,
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

  if (!frame_sink) {
    printf(
        "The sha256 of the output data (including data format "
        "parameters) is:\n");
    for (uint8_t byte : md) {
      printf("%02x", byte);
    }
    printf("\n");
  }

  // ~frame_sink
  // ~main_loop
  return 0;
}
