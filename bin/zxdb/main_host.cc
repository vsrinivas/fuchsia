// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/lib/debug_ipc/helper/buffered_fd.h"
#include "garnet/lib/debug_ipc/helper/message_loop_poll.h"

int main(int argc, char* argv[]) {
  debug_ipc::MessageLoopPoll loop;
  loop.Init();

  // This scope forces all the objects to be destroyed before the Cleanup()
  // call which will mark the message loop as not-current.
  {
    debug_ipc::BufferedFD buffer;
    // TODO(brettw) hook this up to a socket.
    if (!buffer.Init(fxl::UniqueFD())) {
      fprintf(stderr, "Can't hook up stream.");
      return 1;
    }

    // Route data from buffer -> session.
    zxdb::Session session(&buffer.stream());
    buffer.set_data_available_callback(
        [&session](){ session.OnStreamReadable(); });

    zxdb::Console console(&session);
    console.Init();

    loop.Run();
  }

  loop.Cleanup();

  return 0;
}
