// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/test/frame_sink.h>
#include <lib/media/test/one_shot_event.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>

#include <thread>

#include "garnet/examples/media/use_media_decoder/in_stream_file.h"
#include "garnet/examples/media/use_media_decoder/util.h"
#include "in_stream_peeker.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "use_aac_decoder.h"
#include "use_video_decoder.h"

namespace {

// The 8MiB is needed for scanning for h264 start codes, not for VP9 ivf
// headers.  The 8MiB is fairly arbitrary - just meant to be larger than any
// frame size we'll encounter in the test streams we use.  We currently rely on
// finding the next start code within this distance - in future maybe it'd
// become worthwhile to incremenally continue an input AU if we haven't yet
// found the next start code / EOS, in which case this size could be made
// smaller.
constexpr uint32_t kMaxPeekBytes = 8 * 1024 * 1024;

void usage(const char* prog_name) {
  printf(
      "usage: %s (--aac_adts|--h264) [--imagepipe [--fps=<double>]] "
      "<input_file> [<output_file>]\n",
      prog_name);
}

}  // namespace

int main(int argc, char* argv[]) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    printf("fxl::SetLogSettingsFromCommandLine() failed\n");
    exit(-1);
  }
  if (command_line.positional_args().size() < 1 || command_line.positional_args().size() > 2) {
    usage(command_line.argv0().c_str());
    exit(-1);
  }

  async::Loop fidl_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == fidl_loop.StartThread("fidl_thread", &fidl_thread));
  async_dispatcher_t* fidl_dispatcher = fidl_loop.dispatcher();

  // The moment we sys::ComponentContext::Create() + let the
  // fidl_thread retrieve anything from its port, we potentially are letting a
  // request for fuchsia::ui::views::View fail, since it'll fail to find the
  // View service in outgoing_services(), since we haven't yet added View to
  // outgoing_services().  A way to prevent this failure is by not letting
  // fidl_thread read from its port between Create() and
  // outgoing_services()+=View.
  //
  // To that end, we batch up the lambdas we want to run on the fidl_thread,
  // then run them all without returning to read from the port in between.
  //
  // We're intentionally running the fidl_thread separately, partly to
  // intentionally discover and implement example workarounds for this kind of
  // problem.  If you've just got a single thread that will later be used to
  // Run() the fidl dispatching, then this queueing of closures to run in a
  // single batch isn't relevant, since all the code before Run() is already in
  // an equivalent "batch".
  std::vector<fit::closure> to_run_on_fidl_thread;

  std::unique_ptr<sys::ComponentContext> component_context;
  to_run_on_fidl_thread.emplace_back(
      [&component_context] { component_context = sys::ComponentContext::Create(); });

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  fuchsia::sysmem::AllocatorPtr sysmem;
  codec_factory.set_error_handler([](zx_status_t status) {
    // TODO(dustingreen): get and print CodecFactory channel epitaph once that's
    // possible.
    FXL_PLOG(ERROR, status) << "codec_factory failed - unexpected";
  });
  sysmem.set_error_handler(
      [](zx_status_t status) { FXL_PLOG(FATAL, status) << "sysmem failed - unexpected"; });
  to_run_on_fidl_thread.emplace_back(
      [&component_context, fidl_dispatcher, &codec_factory, &sysmem]() mutable {
        component_context->svc()->Connect<fuchsia::mediacodec::CodecFactory>(
            codec_factory.NewRequest(fidl_dispatcher));
        component_context->svc()->Connect<fuchsia::sysmem::Allocator>(
            sysmem.NewRequest(fidl_dispatcher));
      });

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
    if (str_end == str_begin || (frames_per_second == HUGE_VAL && errno == ERANGE)) {
      printf("fps parse error\n");
      usage(command_line.argv0().c_str());
      exit(-1);
    }
  }

  OneShotEvent image_pipe_ready;
  if (use_imagepipe) {
    // We must do this part of setup on the fidl_thread, because we want the
    // FrameSink (or rather, code it uses) to bind to loop (whether explicitly
    // or implicitly), and we want that setup/binding to occur on the same
    // thread as runs that loop (the fidl_thread), as that's a typical
    // assumption of setup/binding code.
    to_run_on_fidl_thread.emplace_back(
        [&fidl_loop, &component_context, frames_per_second, &frame_sink, &image_pipe_ready] {
          frame_sink = FrameSink::Create(
              component_context.get(), &fidl_loop, frames_per_second,
              [&image_pipe_ready](FrameSink* frame_sink) { image_pipe_ready.Signal(); });
        });
  } else {
    // Queue this up since image_pipe_ready is also relied on to ensure that
    // previously-queued lambdas have run.
    to_run_on_fidl_thread.emplace_back([&image_pipe_ready] { image_pipe_ready.Signal(); });
  }

  // Now we can run everything we've queued in to_run_on_fidl_thread.
  PostSerial(fidl_dispatcher, [to_run_on_fidl_thread = std::move(to_run_on_fidl_thread)]() mutable {
    for (auto& to_run : to_run_on_fidl_thread) {
      // Move to local just to delete captures of each lambda before the next
      // one runs, to avoid being brittle in case a lambda starts to care about
      // that.
      fit::closure local_to_run = std::move(to_run);
      local_to_run();
      // ~local_to_run
    }
    // ~to_run_on_fidl_thread deletes some nullptr fit::closure(s)
  });
  ZX_DEBUG_ASSERT(to_run_on_fidl_thread.empty());

  // This also effectively waits until after the lambdas in
  // to_run_on_fidl_thread have run also, since image_pipe_ready can only be
  // signalled after the last lambda in to_run_on_fidl_thread has run.
  image_pipe_ready.Wait(zx::deadline_after(zx::sec(15)));

  auto in_stream_file =
      std::make_unique<InStreamFile>(&fidl_loop, fidl_thread, component_context.get(), input_file);
  auto in_stream_peeker = std::make_unique<InStreamPeeker>(
      &fidl_loop, fidl_thread, component_context.get(), std::move(in_stream_file), kMaxPeekBytes);

  // We set up a closure here just to avoid forcing the two decoder types to
  // take the same parameters, but still be able to share the
  // drive_decoder_thread code below.
  bool is_hash_valid = false;
  fit::closure use_decoder;
  if (command_line.HasOption("aac_adts")) {
    is_hash_valid = true;
    use_decoder = [&fidl_loop, codec_factory = std::move(codec_factory), sysmem = std::move(sysmem),
                   input_file, output_file, &md]() mutable {
      use_aac_decoder(&fidl_loop, std::move(codec_factory), std::move(sysmem), input_file,
                      output_file, md);
    };
  } else if (command_line.HasOption("h264")) {
    use_decoder = [&fidl_loop, fidl_thread, codec_factory = std::move(codec_factory),
                   sysmem = std::move(sysmem), in_stream_peeker = in_stream_peeker.get(),
                   frame_sink = frame_sink.get()]() mutable {
      use_h264_decoder(&fidl_loop, fidl_thread, std::move(codec_factory), std::move(sysmem),
                       in_stream_peeker, /*input_copier*/ nullptr, /*min_output_buffer_size=*/0,
                       /*min_output_buffer_count=*/0,
                       /*is_secure_output=*/false,
                       /*is_secure_input=*/false, frame_sink, /*emit_frame=*/nullptr);
    };
  } else if (command_line.HasOption("vp9")) {
    use_decoder = [&fidl_loop, fidl_thread, codec_factory = std::move(codec_factory),
                   sysmem = std::move(sysmem), in_stream_peeker = in_stream_peeker.get(),
                   frame_sink = frame_sink.get()]() mutable {
      use_vp9_decoder(&fidl_loop, fidl_thread, std::move(codec_factory), std::move(sysmem),
                      in_stream_peeker, /*input_copier*/ nullptr, /*min_output_buffer_size=*/0,
                      /*min_output_buffer_count=*/0,
                      /*is_secure_output=*/false,
                      /*is_secure_input=*/false, frame_sink, /*emit_frame=*/nullptr);
    };
  } else {
    usage(command_line.argv0().c_str());
    return -1;
  }

  use_decoder();

  fidl_loop.Quit();
  fidl_loop.JoinThreads();
  fidl_loop.Shutdown();

  if (is_hash_valid) {
    printf(
        "The sha256 of the output data (including data format "
        "parameters) is:\n");
    for (uint8_t byte : md) {
      printf("%02x", byte);
    }
    printf("\n");
  }

  // ~frame_sink
  // ~fidl_loop
  return 0;
}
