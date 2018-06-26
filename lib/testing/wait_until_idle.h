// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_
#define PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_

#include "gtest/gtest.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fsl/tasks/message_loop.h"

namespace util {

// Convenience invocation of a debug FIDL interface's |WaitUntilIdle() => ()|
// function. This wrapper includes the necessary logic to run the message loop
// while waiting and drain any coincident messages afterwards. It also adds an
// error handler on the debug interface pointer provided, and clears it
// afterwards.
template <class Interface>
void WaitUntilIdle(fidl::InterfacePtr<Interface>* debug_interface_ptr,
                   fsl::MessageLoop* message_loop) {
  debug_interface_ptr->set_error_handler([message_loop] {
    message_loop->PostQuitTask();
    ADD_FAILURE() << Interface::Name_
                  << " disconnected (check app logs for crash)";
  });

  // We can't just use a synchronous ptr because t doesn't run the message loop
  // while it waits.
  (*debug_interface_ptr)->WaitUntilIdle([message_loop] {
    message_loop->PostQuitTask();
  });
  message_loop->Run();
  // Finish processing any remaining messages.
  message_loop->RunUntilIdle();

  debug_interface_ptr->set_error_handler(nullptr);
}

}  // namespace util

#endif  // PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_
