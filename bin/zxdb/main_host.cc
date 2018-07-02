// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/message_loop_poll.h"
#include "garnet/public/lib/fxl/command_line.h"

int main(int argc, char* argv[]) {
  // Get the flags
  std::string out;
  bool quit;
  fxl::CommandLine cmd_line = fxl::CommandLineFromArgcArgv(argc, argv);
  zxdb::Err err = zxdb::ProcessCommandLine(cmd_line, &out, &quit);
  if (err.has_error()) {
    fprintf(stderr, "Error parsing command line: %s\n", err.msg().c_str());
    return 1;
  } else if (!out.empty()) {
    // The command parsing generated output
    printf("%s\n", out.c_str());
  }

  if (quit)
    return 0;

  debug_ipc::MessageLoopPoll loop;
  loop.Init();

  // This scope forces all the objects to be destroyed before the Cleanup()
  // call which will mark the message loop as not-current.
  {
    debug_ipc::BufferedFD buffer;

    // Route data from buffer -> session.
    zxdb::Session session;
    buffer.set_data_available_callback(
        [&session]() { session.OnStreamReadable(); });

    zxdb::Console console(&session);
    console.Init();

    loop.Run();
  }

  loop.Cleanup();

  return 0;
}
