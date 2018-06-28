// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_
#define PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_

#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/interface_ptr.h"

namespace util {

// Convenience invocation of a debug FIDL interface's |WaitUntilIdle() => ()|
// function. This wrapper includes the necessary logic to run the message loop
// while waiting and drain any coincident messages afterwards. It also adds an
// error handler on the debug interface pointer provided, and clears it
// afterwards.
template <class Interface>
void WaitUntilIdle(fidl::InterfacePtr<Interface>* debug_interface_ptr,
                   async::Loop* loop) {
  debug_interface_ptr->set_error_handler([loop] {
    loop->Quit();
    ADD_FAILURE() << Interface::Name_
                  << " disconnected (check app logs for crash)";
  });

  // We can't just use a synchronous ptr or
  // |fidl::InterfacePtr::WaitForResponse| because those don't run the message
  // loop while they wait.
  (*debug_interface_ptr)->WaitUntilIdle([loop] {
    loop->Quit();
  });
  loop->Run();
  loop->ResetQuit();
  // Finish processing any remaining messages.
  loop->RunUntilIdle();
  loop->ResetQuit();

  debug_interface_ptr->set_error_handler(nullptr);
}

}  // namespace util

#endif  // PERIDOT_LIB_TESTING_WAIT_UNTIL_IDLE_H_
