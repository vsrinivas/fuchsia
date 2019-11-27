// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_PARSER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_PARSER_H_

#include <string>
#include <vector>

#include "lib/fit/function.h"

namespace zxdb {

class Command;
class Err;

struct CommandToken {
  CommandToken() = default;
  CommandToken(size_t o, std::string s) : offset(o), str(std::move(s)) {}

  size_t offset = 0;  // Byte offset within the input string.
  std::string str;
};

// Converts the given string to a series of tokens. This is used by ParseCommand and is exposed
// separate for testing purposes.
Err TokenizeCommand(const std::string& input, std::vector<CommandToken>* result);

Err ParseCommand(const std::string& input, Command* output);

// A callback that fills out the command nouns based on the current context. The implementation
// should fill out the target, thread, frame, etc. pointers of the given command structure.
using FillCommandContextCallback = fit::function<void(Command*)>;

// Returns a set of possible completions for the given input. The result will be empty if there are
// none.
//
// The fill_context callback, if set, will be called to fill out the context of a command before
// dispatching to a command-specific completion routine. This lets commands complete based on the
// current target or thread context.
std::vector<std::string> GetCommandCompletions(const std::string& input,
                                               const FillCommandContextCallback& fill_context);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_PARSER_H_
