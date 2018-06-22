// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include <inttypes.h>
#include <stdio.h>
#include <limits>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

Err AssertRunningTarget(ConsoleContext* context, const char* command_name,
                        Target* target) {
  Target::State state = target->GetState();
  if (state == Target::State::kRunning)
    return Err();
  return Err(
      ErrType::kInput,
      fxl::StringPrintf("%s requires a running process but process %d is %s.",
                        command_name, context->IdForTarget(target),
                        TargetStateToString(state).c_str()));
}

Err AssertStoppedThreadCommand(ConsoleContext* context, const Command& cmd,
                               const char* command_name) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (!cmd.thread()) {
    return Err(fxl::StringPrintf(
        "\"%s\" requires a thread but there is no current thread.",
        command_name));
  }
  if (cmd.thread()->GetState() != debug_ipc::ThreadRecord::State::kBlocked &&
      cmd.thread()->GetState() != debug_ipc::ThreadRecord::State::kSuspended) {
    return Err(fxl::StringPrintf(
        "\"%s\" requires a suspended thread but thread %d is %s.\n"
        "To view and sync thread state with the remote system, type "
        "\"thread\".",
        command_name, context->IdForThread(cmd.thread()),
        ThreadStateToString(cmd.thread()->GetState()).c_str()));
  }
  return Err();
}

Err StringToUint64(const std::string& s, uint64_t* out) {
  *out = 0;
  if (s.empty())
    return Err(ErrType::kInput, "The empty string is not a number.");

  bool is_hex = s.size() > 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  if (is_hex) {
    for (size_t i = 2; i < s.size(); i++) {
      if (!isxdigit(s[i]))
        return Err(ErrType::kInput, "Invalid hex number: + \"" + s + "\".");
    }
  } else {
    for (size_t i = 0; i < s.size(); i++) {
      if (!isdigit(s[i]))
        return Err(ErrType::kInput, "Invalid number: \"" + s + "\".");
    }
  }

  *out = strtoull(s.c_str(), nullptr, is_hex ? 16 : 10);
  return Err();
}

Err ReadUint64Arg(const Command& cmd, size_t arg_index, const char* param_desc,
                  uint64_t* out) {
  if (cmd.args().size() <= arg_index) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("Not enough arguments when reading the %s.",
                                 param_desc));
  }
  Err result = StringToUint64(cmd.args()[arg_index], out);
  if (result.has_error()) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("Invalid number \"%s\" when reading the %s.",
                                 cmd.args()[arg_index].c_str(), param_desc));
  }
  return Err();
}

Err ParseHostPort(const std::string& in_host, const std::string& in_port,
                  std::string* out_host, uint16_t* out_port) {
  if (in_host.empty())
    return Err(ErrType::kInput, "No host component specified.");
  if (in_port.empty())
    return Err(ErrType::kInput, "No port component specified.");

  // Trim brackets from the host name for IPv6 addresses.
  if (in_host.front() == '[' && in_host.back() == ']')
    *out_host = in_host.substr(1, in_host.size() - 2);
  else
    *out_host = in_host;

  // Re-use paranoid int64 parsing.
  uint64_t port64;
  Err err = StringToUint64(in_port, &port64);
  if (err.has_error())
    return err;
  if (port64 == 0 || port64 > std::numeric_limits<uint16_t>::max())
    return Err(ErrType::kInput, "Port value out of range.");
  *out_port = static_cast<uint16_t>(port64);

  return Err();
}

Err ParseHostPort(const std::string& input, std::string* out_host,
                  uint16_t* out_port) {
  // Separate based on the last colon.
  size_t colon = input.rfind(':');
  if (colon == std::string::npos)
    return Err(ErrType::kInput, "Expected colon to separate host/port.");

  // If the host has a colon in it, it could be an IPv6 address. In this case,
  // require brackets around it to differentiate the case where people
  // supplied an IPv6 address and we just picked out the last component above.
  std::string host = input.substr(0, colon);
  if (host.empty())
    return Err(ErrType::kInput, "No host component specified.");
  if (host.find(':') != std::string::npos) {
    if (host.front() != '[' || host.back() != ']') {
      return Err(ErrType::kInput,
                 "For IPv6 addresses use either: \"[::1]:1234\"\n"
                 "or the two-parameter form: \"::1 1234");
    }
  }

  std::string port = input.substr(colon + 1);

  return ParseHostPort(host, port, out_host, out_port);
}

std::string TargetStateToString(Target::State state) {
  struct Mapping {
    Target::State state;
    const char* string;
  };
  static const Mapping mappings[] = {{Target::State::kNone, "Not running"},
                                     {Target::State::kStarting, "Starting"},
                                     {Target::State::kRunning, "Running"}};

  for (const Mapping& mapping : mappings) {
    if (mapping.state == state)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string ThreadStateToString(debug_ipc::ThreadRecord::State state) {
  struct Mapping {
    debug_ipc::ThreadRecord::State state;
    const char* string;
  };
  static const Mapping mappings[] = {
      {debug_ipc::ThreadRecord::State::kNew, "New"},
      {debug_ipc::ThreadRecord::State::kRunning, "Running"},
      {debug_ipc::ThreadRecord::State::kSuspended, "Suspended"},
      {debug_ipc::ThreadRecord::State::kBlocked, "Blocked"},
      {debug_ipc::ThreadRecord::State::kDying, "Dying"},
      {debug_ipc::ThreadRecord::State::kDead, "Dead"}};

  for (const Mapping& mapping : mappings) {
    if (mapping.state == state)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const BreakpointSettings& settings) {
  switch (settings.scope) {
    case BreakpointSettings::Scope::kSystem:
      return "Global";
    case BreakpointSettings::Scope::kTarget:
      return fxl::StringPrintf("pr %d",
                               context->IdForTarget(settings.scope_target));
    case BreakpointSettings::Scope::kThread:
      return fxl::StringPrintf(
          "pr %d t %d",
          context->IdForTarget(
              settings.scope_thread->GetProcess()->GetTarget()),
          context->IdForThread(settings.scope_thread));
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string BreakpointStopToString(BreakpointSettings::StopMode mode) {
  switch (mode) {
    case BreakpointSettings::StopMode::kNone:
      return "None";
    case BreakpointSettings::StopMode::kThread:
      return "Thread";
    case BreakpointSettings::StopMode::kProcess:
      return "Process";
    case BreakpointSettings::StopMode::kAll:
      return "All";
  }
  FXL_NOTREACHED();
  return std::string();
}

const char* BreakpointEnabledToString(bool enabled) {
  return enabled ? "Enabled" : "Disabled";
}

std::string ExceptionTypeToString(debug_ipc::NotifyException::Type type) {
  struct Mapping {
    debug_ipc::NotifyException::Type type;
    const char* string;
  };
  static const Mapping mappings[] = {
      {debug_ipc::NotifyException::Type::kGeneral, "General"},
      {debug_ipc::NotifyException::Type::kHardware, "Hardware"},
      {debug_ipc::NotifyException::Type::kSoftware, "Software"}};
  for (const Mapping& mapping : mappings) {
    if (mapping.type == type)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string DescribeTarget(const ConsoleContext* context,
                           const Target* target) {
  int id = context->IdForTarget(target);
  std::string state = TargetStateToString(target->GetState());

  // Koid string. This includes a trailing space when present so it can be
  // concat'd even when not present and things look nice.
  std::string koid_str;
  if (target->GetState() == Target::State::kRunning) {
    koid_str =
        fxl::StringPrintf("koid=%" PRIu64 " ", target->GetProcess()->GetKoid());
  }

  std::string result = fxl::StringPrintf("Process %d %s %s", id, state.c_str(),
                                         koid_str.c_str());
  result += DescribeTargetName(target);
  return result;
}

std::string DescribeTargetName(const Target* target) {
  // When running, use the object name if any.
  std::string name;
  if (target->GetState() == Target::State::kRunning)
    name = target->GetProcess()->GetName();

  // Otherwise fall back to the program name which is the first arg.
  if (name.empty()) {
    const std::vector<std::string>& args = target->GetArgs();
    if (!args.empty())
      name += args[0];
  }
  return name;
}

std::string DescribeThread(const ConsoleContext* context,
                           const Thread* thread) {
  return fxl::StringPrintf("Thread %d %s koid=%" PRIu64 " %s",
                           context->IdForThread(thread),
                           ThreadStateToString(thread->GetState()).c_str(),
                           thread->GetKoid(), thread->GetName().c_str());
}

// Unlike the other describe command, this takes an ID because normally
// you know the index when calling into here, and it's inefficient to look up.
std::string DescribeFrame(const Frame* frame, int id) {
  // This will need symbols hooked up.
  return fxl::StringPrintf("Frame %d ", id) +
         DescribeLocation(frame->GetLocation(), false);
}

std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint) {
  BreakpointSettings settings = breakpoint->GetSettings();

  std::string scope = BreakpointScopeToString(context, settings);
  std::string stop = BreakpointStopToString(settings.stop_mode);
  const char* enabled = BreakpointEnabledToString(settings.enabled);
  std::string location = DescribeInputLocation(settings.location);

  return fxl::StringPrintf("Breakpoint %d on %s, %s, stop=%s, @ %s",
                           context->IdForBreakpoint(breakpoint), scope.c_str(),
                           enabled, stop.c_str(), location.c_str());
}

std::string DescribeInputLocation(const InputLocation& location) {
  switch (location.type) {
    case InputLocation::Type::kNone:
      return "<no location>";
    case InputLocation::Type::kLine:
      return DescribeFileLine(location.line);
    case InputLocation::Type::kSymbol:
      return location.symbol;
    case InputLocation::Type::kAddress:
      return fxl::StringPrintf("0x%" PRIx64, location.address);
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string DescribeLocation(const Location& loc, bool always_show_address) {
  if (!loc.is_valid())
    return "<invalid address>";
  if (!loc.has_symbols())
    return fxl::StringPrintf("0x%" PRIx64, loc.address());

  std::string result;
  if (always_show_address)
    result = fxl::StringPrintf("0x%" PRIx64 ", ", loc.address());

  if (!loc.function().empty())
    result += loc.function() + "() " + GetBullet() + " ";
  result += DescribeFileLine(loc.file_line());
  return result;
}

std::string DescribeFileLine(const FileLine& file_line, bool show_path) {
  return fxl::StringPrintf("%s:%d",
                           show_path ? file_line.file().c_str()
                                     : file_line.GetFileNamePart().c_str(),
                           file_line.line());
}

}  // namespace zxdb
