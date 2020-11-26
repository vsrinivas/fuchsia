// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains apis needed for inspection of on-disk data structures.

#include "disk_inspector/command.h"

#include <iterator>
#include <sstream>
#include <utility>

#include <fs/trace.h>

namespace disk_inspector {

std::string PrintCommand(const Command& command) {
  std::ostringstream os;
  os << command.name;
  for (const auto& field : command.fields) {
    os << " [" << field.name << "]";
  }
  return os.str();
}

std::string PrintCommandList(const std::vector<Command>& commands) {
  std::ostringstream os;
  for (const auto& command : commands) {
    os << command.name;
    for (const auto& field : command.fields) {
      os << " [" << field.name << "]";
    }
    os << "\n";
    os << "\t" << command.help_message << "\n";
    for (const auto& field : command.fields) {
      os << "\t\t" << field.name << ": " << field.help_message << "\n";
    }
    os << "\n";
  }
  return os.str();
}

fit::result<ParsedCommand, zx_status_t> ParseCommand(const std::vector<std::string>& args,
                                                     const Command& command) {
  ZX_DEBUG_ASSERT(!args.empty() && args[0] == command.name);
  ParsedCommand parsed_args;
  parsed_args.name = args[0];
  if (command.fields.size() != args.size() - 1) {
    FS_TRACE_ERROR(
        "Number of arguments provided(%lu) does not match number of arguments needed(%lu)\n",
        args.size() - 1, command.fields.size());
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  for (uint32_t i = 0; i < command.fields.size(); ++i) {
    Field field = command.fields[i];
    ZX_DEBUG_ASSERT(parsed_args.string_fields.find(field.name) == parsed_args.string_fields.end());
    ZX_DEBUG_ASSERT(parsed_args.uint64_fields.find(field.name) == parsed_args.uint64_fields.end());
    switch (field.type) {
      case ArgType::kString: {
        parsed_args.string_fields[field.name] = args[i + 1];
        break;
      }
      case ArgType::kUint64: {
        char* endptr;
        uint64_t value = std::strtoull(args[i + 1].c_str(), &endptr, 10);
        if (*endptr != '\0') {
          FS_TRACE_ERROR("Argument %s cannot be converted to uint64 (value: %s)\n",
                         field.name.c_str(), args[i].c_str());
          return fit::error(ZX_ERR_INVALID_ARGS);
        }
        parsed_args.uint64_fields[field.name] = value;
        break;
      }
      default: {
        FS_TRACE_ERROR("Command parsing reached unknown ArgType.\n");
        return fit::error(ZX_ERR_NOT_SUPPORTED);
      }
    }
  }
  return fit::ok(parsed_args);
}

}  // namespace disk_inspector
