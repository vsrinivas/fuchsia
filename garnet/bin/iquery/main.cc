// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <iostream>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/promise.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>

#include "garnet/bin/iquery/formatter.h"
#include "garnet/bin/iquery/modes.h"
#include "garnet/public/lib/inspect/discovery/object_source.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  async::Executor executor(loop.dispatcher());

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  iquery::Options options(command_line);
  if (!options.Valid()) {
    return 1;
  }

  if (!options.chdir.empty()) {
    if (chdir(options.chdir.c_str()) != 0) {
      FXL_LOG(ERROR) << "Failed to chdir to " << options.chdir;
    }
  }

  if (command_line.HasOption("help") || options.paths.size() == 0) {
    options.Usage(command_line.argv0());
    return 0;
  }

  fit::promise<std::vector<inspect::ObjectSource>> results;
  // Dispatch to the correct mode.
  if (options.mode == iquery::Options::Mode::CAT) {
    results = iquery::RunCat(&options);
  } else if (options.mode == iquery::Options::Mode::FIND) {
    results = iquery::RunFind(&options);
  } else if (options.mode == iquery::Options::Mode::LS) {
    results = iquery::RunLs(&options);
  } else {
    FXL_LOG(ERROR) << "Unsupported mode";
    return 1;
  }

  executor.schedule_task(
      results
          .and_then(
              [&options, &loop](std::vector<inspect::ObjectSource>& results) {
                // Sort the hierarchies if requested.
                if (options.sort) {
                  for (auto& source : results) {
                    source.SortHierarchy();
                  }
                }
                // Formatter will handle the correct case according to the
                // options values.
                std::cout << options.formatter->Format(options, results);
                loop.Quit();
              })
          .or_else([&loop] {
            FXL_LOG(ERROR) << "An error occurred";
            loop.Quit();
          }));

  loop.Run();

  return 0;
}
