// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/strings/string_number_conversions.h>

#include <trace-provider/provider.h>
#include <trace/event.h>

static constexpr int kDefaultCount = 1;
static constexpr int kDefaultDelaySeconds = 2;
static constexpr int kDefaultDurationSeconds = 10;

static const char* prog_name;

static void PrintHelp(FILE* f) {
  fprintf(f, "Usage: %s [options]\n", prog_name);
  fprintf(f, "Options:\n");
  fprintf(f, "  --help             Duh ...\n");
  fprintf(f, "  --count=COUNT      Specify number of records per iteration\n");
  fprintf(f, "                     The default is %d.\n",
          kDefaultCount);
  fprintf(f, "  --delay=SECONDS    Delay SECONDS before starting\n");
  fprintf(f, "                     This is useful until TO-650 is fixed.\n");
  fprintf(f, "                     The default is %d.\n",
          kDefaultDelaySeconds);
  fprintf(f, "  --duration=SECONDS Specify time to run, in seconds\n");
  fprintf(f, "                     The default is %d.\n",
          kDefaultDurationSeconds);
  fprintf(f, "  --quiet[=LEVEL]    Set quietness level (opposite of verbose)\n");
  fprintf(f, "  --verbose[=LEVEL]  Set debug verbosity level\n");
}

static void RunStressTestIteration(int count) {
  // Simulate some kind of workload.
  FXL_LOG(INFO) << "Doing work!";

  static const char kSomethingCategory[] = "stress:something";
  static const char kWithZeroArgs[] = "with-zero-args";
  static const char kWithOneArg[] = "with-one-arg";
  static const char kWithTwoArgs[] = "with-two-args";

  for (int i = 0; i < count; ++i) {
    // Add some variety.
    const char* event_name = nullptr;
    switch (i % 3) {
    case 0:
      event_name = kWithZeroArgs;
      TRACE_DURATION_BEGIN(kSomethingCategory, event_name);
      break;
    case 1:
      event_name = kWithOneArg;
      TRACE_DURATION_BEGIN(kSomethingCategory, event_name, "k1", 1);
      break;
    case 2:
      event_name = kWithTwoArgs;
      TRACE_DURATION_BEGIN(kSomethingCategory, event_name,
                           "k1", 1, "k2", 2.0);
      break;
    default:
      __UNREACHABLE;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(1)));
    TRACE_DURATION_END(kSomethingCategory, event_name);
  }
}

int main(int argc, char** argv) {
  prog_name = argv[0];
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return 1;

  if (cl.HasOption("help", nullptr)) {
    PrintHelp(stdout);
    return EXIT_SUCCESS;
  }

  int count = kDefaultCount;
  int delay = kDefaultDelaySeconds;
  int duration = kDefaultDurationSeconds;
  std::string arg;

  if (cl.GetOptionValue("count", &arg)) {
    if (!fxl::StringToNumberWithError<int>(arg, &count) ||
        count < 0) {
      FXL_LOG(ERROR) << "Invalid count: " << arg;
      return EXIT_FAILURE;
    }
  }

  if (cl.GetOptionValue("delay", &arg)) {
    if (!fxl::StringToNumberWithError<int>(arg, &delay) ||
        delay < 0) {
      FXL_LOG(ERROR) << "Invalid delay: " << arg;
      return EXIT_FAILURE;
    }
  }

  if (cl.GetOptionValue("duration", &arg)) {
    if (!fxl::StringToNumberWithError<int>(arg, &duration) ||
        duration < 0) {
      FXL_LOG(ERROR) << "Invalid duration: " << arg;
      return EXIT_FAILURE;
    }
  }

  // Use a separate loop for the provider.
  // This is in anticipation of double-buffering support.
  async::Loop provider_loop(&kAsyncLoopConfigNoAttachToThread);
  provider_loop.StartThread("TraceProvider");
  trace::TraceProvider provider(provider_loop.dispatcher());

  if (delay > 0) {
    FXL_LOG(INFO) << "Trace stressor delaying " << delay << " seconds ...";
    zx::nanosleep(zx::deadline_after(zx::sec(delay)));
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  zx::time start_time = async::Now(loop.dispatcher());
  zx::time quit_time = start_time + zx::sec(duration);

  FXL_LOG(INFO) << "Trace stressor doing work for " << duration
                << " seconds ...";

  int iteration = 0;
  async::TaskClosure task([&loop, &task, &iteration, count, quit_time] {
      TRACE_DURATION("stress:example", "Doing Work!", "iteration", ++iteration);

      RunStressTestIteration(count);

      zx::nanosleep(zx::deadline_after(zx::msec(500)));

      // Stop if quitting.
      zx::time now = async::Now(loop.dispatcher());
      if (now > quit_time) {
        loop.Quit();
        return;
      }

      // Schedule more work in a little bit.
      task.PostForTime(loop.dispatcher(), now + zx::msec(200));
  });
  task.PostForTime(loop.dispatcher(), start_time);

  loop.Run();

  FXL_LOG(INFO) << "Trace stressor finished";

  // Cleanly shutdown the provider thread.
  provider_loop.Quit();
  provider_loop.JoinThreads();

  return EXIT_SUCCESS;
}
