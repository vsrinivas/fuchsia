// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains apis needed for inspection of on-disk data structures.

#ifndef DISK_INSPECTOR_COMMAND_H_
#define DISK_INSPECTOR_COMMAND_H_

#include <lib/fit/result.h>
#include <zircon/types.h>

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <block-client/cpp/block-device.h>

namespace disk_inspector {

// Represent argument types that the function represented by the command
// can use.
enum class ArgType {
  kString,
  kUint64,
};

// Struct representing the field of a command, its type, and description of itself.
struct Field {
  std::string name;
  ArgType type;
  std::string help_message;
};

// Struct representing a command string in which the fields are parsed to
// their specific types. The values in this struct should only be used
// if the status representing whether parsing succeeded or failed is ZX_OK.
struct ParsedCommand {
  std::string name;
  std::unordered_map<std::string, std::string> string_fields;
  std::unordered_map<std::string, uint64_t> uint64_fields;
};

using CommandFunction = std::function<zx_status_t(ParsedCommand)>;

// Struct representing a command with its name, list of fields, description of
// what it does, and a wrapper lambda taking in a ParsedCommand to call the
// actual function with the parsed arguments.
struct Command {
  std::string name;
  std::vector<Field> fields;
  std::string help_message;
  CommandFunction function;
};

// Returns a string representing a command in the form of:
// <command name> [<field 0 name>] [<field 1 name>]...
std::string PrintCommand(const Command& command);

// Returns a string representing a command in the form of:
// <command 0 name> [<field 0 name>] [<field 1 name>]...
// <command 1 name> [<field 0 name>] [<field 1 name>]...
// ...
std::string PrintCommandList(const std::vector<Command>& commands);

// Parses a vector of string |args| into the typed fields of |command| as
// a CommandArgs struct holding the typed field mappings. |args| should be the
// full command vector including both the command name and args. Sets the status
// of the returned status field in CommandArgs to an error if:
// - The number of arguments in |args| does not match the number of fields in
//   |command|.
// - An argument cannot be parsed in the type specified by the field in the
//   command.
// - An argument type in the command is not supported for parsing.
// Asserts the passed in |args| is not empty and that the command name in |args|
// matches that of the |command|.
fit::result<ParsedCommand, zx_status_t> ParseCommand(const std::vector<std::string>& args,
                                                     const Command& command);

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_COMMAND_H_
