// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller_parser.h"

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include "src/lib/fxl/command_line.h"

namespace camera {

using Command = fuchsia::camera::gym::Command;
using SetConfigCommand = fuchsia::camera::gym::SetConfigCommand;
using AddStreamCommand = fuchsia::camera::gym::AddStreamCommand;
using SetCropCommand = fuchsia::camera::gym::SetCropCommand;
using SetResolutionCommand = fuchsia::camera::gym::SetResolutionCommand;

ControllerParser::ControllerParser(std::string app) : app_(app) {}

fit::result<std::vector<Command>> ControllerParser::ParseArgcArgv(int argc, const char** argv) {
  ZX_ASSERT(argc > 0);
  std::vector<Command> commands;
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  for (const auto& option : cl.options()) {
    auto result = ParseOneCommand(option.name, option.value);
    if (result.is_error()) {
      return fit::error();
    }
    commands.emplace_back(result.take_value());
  }
  return fit::ok(std::move(commands));
}

fit::result<Command> ControllerParser::ParseOneCommand(const std::string& name,
                                                       const std::string& value) {
  std::string args;
  if (name.compare("set-config") == 0) {
    auto result = ParseSetConfigCommand(name, value);
    if (result.is_ok()) {
      SetConfigCommand set_config(result.take_value());
      Command command = Command::WithSetConfig(std::move(set_config));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("set-config-async") == 0) {
    auto result = ParseSetConfigCommand(name, value, true);
    if (result.is_ok()) {
      SetConfigCommand set_config(result.take_value());
      Command command = Command::WithSetConfig(std::move(set_config));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("add-stream") == 0) {
    auto result = ParseAddStreamCommand(name, value);
    if (result.is_ok()) {
      AddStreamCommand add_stream = result.take_value();
      Command command = Command::WithAddStream(std::move(add_stream));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("add-stream-async") == 0) {
    auto result = ParseAddStreamCommand(name, value, true);
    if (result.is_ok()) {
      AddStreamCommand add_stream = result.take_value();
      Command command = Command::WithAddStream(std::move(add_stream));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("set-crop") == 0) {
    auto result = ParseSetCropCommand(name, value);
    if (result.is_ok()) {
      SetCropCommand set_crop = result.take_value();
      Command command = Command::WithSetCrop(std::move(set_crop));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("set-crop-async") == 0) {
    auto result = ParseSetCropCommand(name, value, true);
    if (result.is_ok()) {
      SetCropCommand set_crop = result.take_value();
      Command command = Command::WithSetCrop(std::move(set_crop));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("set-resolution") == 0) {
    auto result = ParseSetResolutionCommand(name, value);
    if (result.is_ok()) {
      SetResolutionCommand set_resolution = result.take_value();
      Command command = Command::WithSetResolution(std::move(set_resolution));
      return fit::ok(std::move(command));
    }
  } else if (name.compare("set-resolution-async") == 0) {
    auto result = ParseSetResolutionCommand(name, value, true);
    if (result.is_ok()) {
      SetResolutionCommand set_resolution = result.take_value();
      Command command = Command::WithSetResolution(std::move(set_resolution));
      return fit::ok(std::move(command));
    }
  } else {
    fprintf(stderr, "%s: Command not understood: \"%s\"\n", app_.c_str(), name.c_str());
  }
  return fit::error();
}

fit::result<SetConfigCommand> ControllerParser::ParseSetConfigCommand(const std::string& name,
                                                                      const std::string& value,
                                                                      bool async) {
  auto result = ParseValues(value, "i");
  if (result.is_error()) {
    fprintf(stderr, "%s: Failed to parse arguments: \"%s\"\n", app_.c_str(), name.c_str());
    return fit::error();
  }
  ValuesArray values = result.take_value();
  SetConfigCommand set_config;
  set_config.config_id = values.i[0];
  set_config.async = async;
  return fit::ok(std::move(set_config));
}

fit::result<AddStreamCommand> ControllerParser::ParseAddStreamCommand(const std::string& name,
                                                                      const std::string& value,
                                                                      bool async) {
  auto result = ParseValues(value, "i");
  if (result.is_error()) {
    fprintf(stderr, "%s: Failed to parse arguments: \"%s\"\n", app_.c_str(), name.c_str());
    return fit::error();
  }
  ValuesArray values = result.take_value();
  AddStreamCommand add_stream;
  add_stream.stream_id = values.i[0];
  add_stream.async = async;
  return fit::ok(add_stream);
}

fit::result<SetCropCommand> ControllerParser::ParseSetCropCommand(const std::string& name,
                                                                  const std::string& value,
                                                                  bool async) {
  auto result = ParseValues(value, "iffff");
  if (result.is_error()) {
    fprintf(stderr, "%s: Failed to parse arguments: \"%s\"\n", app_.c_str(), name.c_str());
    return fit::error();
  }
  ValuesArray values = result.take_value();
  SetCropCommand set_crop;
  set_crop.stream_id = values.i[0];
  set_crop.x = values.f[1];
  set_crop.y = values.f[2];
  set_crop.width = values.f[3];
  set_crop.height = values.f[4];
  set_crop.async = async;
  return fit::ok(set_crop);
}

fit::result<SetResolutionCommand> ControllerParser::ParseSetResolutionCommand(
    const std::string& name, const std::string& value, bool async) {
  auto result = ParseValues(value, "iii");
  if (result.is_error()) {
    fprintf(stderr, "%s: Failed to parse arguments: \"%s\"\n", app_.c_str(), name.c_str());
    return fit::error();
  }
  ValuesArray values = result.take_value();
  SetResolutionCommand set_resolution;
  set_resolution.stream_id = values.i[0];
  set_resolution.width = values.i[1];
  set_resolution.height = values.i[2];
  set_resolution.async = async;
  return fit::ok(set_resolution);
}

// TODO(?????) - Should we tolerate the case where more than enough args are present?
fit::result<camera::ControllerParser::ValuesArray> ControllerParser::ParseValues(
    const std::string& args, const char* types) {
  char buffer[args.size() + 1];
  strcpy(buffer, args.c_str());
  char* start_ptr = buffer;
  char* private_ptr;

  // This implementation prioritizes ',' as a separator character first.
  const char* delim = ",";

  ValuesArray values;
  for (size_t count = 0; count <= MAX_VALUES; count++) {
    // Extract current value string.
    char* token_ptr = strtok_r(start_ptr, delim, &private_ptr);

    // If this is the end of the types[] list, then make sure the args string ended also.
    if (types[count] == '\0') {
      if (!token_ptr || (*token_ptr == '\0')) {
        return fit::ok(values);
      }
      return fit::error();  // ERROR: Extraneous characters at end of argument list.
    }

    // Since another token is expected, make sure there is still something to parse.
    if (!token_ptr || (*token_ptr == '\0')) {
      return fit::error();  // ERROR: Premature end-of-string
    }

    // What type of argument value is expected?
    char* stop_ptr;
    switch (types[count]) {
      case 'i':  // uint32_t
        values.i[count] = static_cast<uint32_t>(strtoul(token_ptr, &stop_ptr, 0));
        break;
      case 'f':  // float
        values.f[count] = strtof(token_ptr, &stop_ptr);
        break;
      default:
        ZX_ASSERT(false);  // FATAL: Caller must have passed an invalid "types" string.
    }

    // Did something get parsed?
    if (stop_ptr == token_ptr) {
      return fit::error();  // ERROR: No valid value characters found
    }
    ZX_ASSERT(stop_ptr);
    if (*stop_ptr != '\0') {
      return fit::error();  // ERROR: Excess characters at the end
    }
    // Tell strtok_r() it is now a 2nd or subsequent pass.
    start_ptr = nullptr;
  }
  ZX_ASSERT(false);  // Should never get here.
  return fit::error();
}

}  // namespace camera
