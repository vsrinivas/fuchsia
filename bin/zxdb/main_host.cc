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
#include "garnet/public/lib/fxl/strings/string_printf.h"

// Defined below. Main should be on top.
void ScheduleActions(zxdb::Session&, zxdb::Console&,
                     std::vector<zxdb::Action>&&);

int main(int argc, char* argv[]) {
  // Process the cmd line and get any actions required to run at startup.
  std::vector<zxdb::Action> actions;
  zxdb::Err err;
  fxl::CommandLine cmd_line = fxl::CommandLineFromArgcArgv(argc, argv);
  zxdb::FlagProcessResult flag_res =
      zxdb::ProcessCommandLine(cmd_line, &err, &actions);
  if (flag_res == zxdb::FlagProcessResult::kError) {
    fprintf(stderr, "Error parsing command line flags: %s\n",
            err.msg().c_str());
    return 1;
  } else if (flag_res == zxdb::FlagProcessResult::kQuit) {
    return 0;
  }

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

    if (flag_res == zxdb::FlagProcessResult::kActions) {
      ScheduleActions(session, console, std::move(actions));
    } else {
      // Interactive mode is the default mode.
      console.Init();
    }

    loop.Run();
  }

  loop.Cleanup();

  return 0;
}

void ScheduleActions(zxdb::Session& session, zxdb::Console& console,
                     std::vector<zxdb::Action>&& actions) {
  auto callback = [&](zxdb::Err err) {
    std::string msg;
    if (!err.has_error()) {
      msg = "All actions were executed successfully.";
    } else if (err.type() == zxdb::ErrType::kCanceled) {
      msg = "Action processing was cancelled.";
    } else {
      msg = fxl::StringPrintf("Error executing actions: %s", err.msg().c_str());
    }
    // We go into interactive mode.
    console.Init();
  };

  // This will add the actions to the MessageLoop and oversee that all the
  // actions run or the flow is interrupted if one of them fails.
  // Actions run on a singleton ActionFlow instance.
  zxdb::ActionFlow& flow = zxdb::ActionFlow::Singleton();
  flow.ScheduleActions(std::move(actions), &session, &console, callback);
}
