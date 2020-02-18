// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains apis needed for inspection of on-disk data structures.

#ifndef DISK_INSPECTOR_COMMAND_HANDLER_H_
#define DISK_INSPECTOR_COMMAND_HANDLER_H_

#include <zircon/types.h>

#include <string>
#include <vector>

#include <block-client/cpp/block-device.h>

namespace disk_inspector {

// Interface for each filesystem inspect library to implement to
// expose string-based commands to inspect and modify the filesystem
// for debugging purposes.
class CommandHandler {
 public:
  virtual ~CommandHandler() = default;

  // Prints out the supported commands for this inspector. Implementers should
  // document what each supported function does and what args it takes.
  virtual void PrintSupportedCommands() = 0;

  // Calls a function in the list of supported functions with the required arguments.
  // Each function should perform the necessary parsing from string to whatever they need.
  // Return an error status if the function is not supported or if the arguments
  // are incorrect for the associated function.
  virtual zx_status_t CallCommand(std::vector<std::string> command) = 0;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_COMMAND_HANDLER_H_
