// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_CONSOLE_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_CONSOLE_H_

#include <fuchsia/hardware/pty/c/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/unsafe.h>

#include <string>
#include <vector>

#include "src/developer/shell/console/command.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/line_input/modal_line_input.h"

namespace shell::console {

class Console {
 public:
  class Client {
   public:
    Client() = default;

    // A |command| has been read from the console.
    //
    // May return ZX_ERR_STOP, ZX_ERR_NEXT, or ZX_ERR_ASYNC.
    //
    // If this function returns |ZX_ERR_STOP|, no further commands will be read
    // from the console.
    //
    // If this function returns |ZX_ERR_NEXT|, the console will continue to read
    // commands from the console.
    //
    // If this function returns |ZX_ERR_ASYNC|, the console will wait to read
    // further commands from the console until the |GetNextCommand| method is
    // called on the console.
    //
    // If this function returns anything else, the result will be treated as a
    // ZX_ERR_NEXT, but the resulting error message will be printed to the
    // console.
    virtual Err OnConsoleCommand(std::unique_ptr<Command> command) = 0;

    // The console has been asked to interrupt the current command.
    //
    // Called only between |OnConsoleCommand| returning |ZX_ERR_ASYNC| and
    // |GetNextCommand|.
    virtual void OnConsoleInterrupt() = 0;

    // The console has encountered an error.
    //
    // No further commands can be read from the console.
    //
    // If the console reaches the end of the input stream, |status| will be
    // |ZX_ERR_PEER_CLOSED|.
    virtual void OnConsoleError(zx_status_t status) = 0;

   protected:
    virtual ~Client();
  };

  // Create an interactive console.
  //
  // Reads input from the |input_fd| file description, which is typically
  // |STDIN_FILENO| but can be set to another valid file descriptor for testing.
  //
  // Uses |dispatcher| to schedule asynchronous waits on |input_fd|.
  Console(Client* client, async_dispatcher_t* dispatcher, int input_fd);
  ~Console();

  // Initialize the console.
  //
  // The given |prompt| is displayed to the user when the user is expected to
  // input another command.
  //
  // Does not prompt the user to input a command. Call |GetNextCommand| to get
  // the first command from the user.
  void Init(std::string prompt);

  // Get the next command from the user.
  //
  // This operation completes asynchronously by calling methods on the |client|
  // provided to the constructor. A single call to |GetNextCommand| will result
  // in one or more calls to |OnConsoleCommand| and at most one call to
  // |OnConsoleError|. See |Client| for more information.
  //
  // It is an error to call |GetNextCommand| again until |OnConsoleCommand| has
  // returned |ZX_ERR_ASYNC|.
  void GetNextCommand();

 private:
  void WaitForInputAsynchronously();
  void WaitForInterruptAsynchronously();
  void OnAccept(const std::string& line);
  void OnError(zx_status_t status);

  Client* client_;
  int input_fd_;
  fsl::FDWaiter input_waiter_;

  fdio_t* tty_ = nullptr;
  fsl::FDWaiter interrupt_waiter_;

  line_input::ModalLineInputStdout line_input_;

  bool should_read_ = false;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_CONSOLE_H_
