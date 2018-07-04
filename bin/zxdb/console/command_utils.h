// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <string>

#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Breakpoint;
class Command;
class ConsoleContext;
class Err;
class Frame;
struct InputLocation;
class Location;
class Thread;

// Ensures the target is currently running (it has a current Process associated
// with it. If not, generates an error of the form
// "<command_name> requires a running target".
Err AssertRunningTarget(ConsoleContext* context, const char* command_name,
                        Target* target);

// Validates a command that applies to a stopped thread:
//
// The thread on the command must exist and be stopped.
//
// If validate_nouns is set, only thread and process nouns may be specified
// (these are most common for commands that operate on threads).
//
// If not, generates an error of the form "<command_name> requires a stopped
// target".
Err AssertStoppedThreadCommand(ConsoleContext* context, const Command& cmd,
                               bool validate_nouns, const char* command_name);

[[nodiscard]] Err StringToInt(const std::string& s, int* out);
[[nodiscard]] Err StringToUint32(const std::string& s, uint32_t* out);
[[nodiscard]] Err StringToUint64(const std::string& s, uint64_t* out);

// Reads an int64 from the given index of the command args. Returns an error
// if there are not enough args, or if the value isn't an int64.
//
// The param_desc will be used in the error string, for example "process koid".
[[nodiscard]] Err ReadUint64Arg(const Command& cmd, size_t arg_index,
                                const char* param_desc, uint64_t* out);

// Parses a host and port. The two-argument version assumes the host and
// port are given separately. The one-argument version assumes they're
// separated by a colon.
Err ParseHostPort(const std::string& in_host, const std::string& in_port,
                  std::string* out_host, uint16_t* out_port);
Err ParseHostPort(const std::string& input, std::string* out_host,
                  uint16_t* out_port);

std::string TargetStateToString(Target::State state);
std::string ThreadStateToString(debug_ipc::ThreadRecord::State state);

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const BreakpointSettings& settings);
std::string BreakpointStopToString(BreakpointSettings::StopMode mode);
const char* BreakpointEnabledToString(bool enabled);

std::string ExceptionTypeToString(debug_ipc::NotifyException::Type type);

std::string DescribeTarget(const ConsoleContext* context, const Target* target);

// Returns the process name of the given target, depending on the running
// process or the current app name, as applicable.
std::string DescribeTargetName(const Target* target);

std::string DescribeThread(const ConsoleContext* context, const Thread* thread);
std::string DescribeFrame(const Frame* frame, int id);
std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint);

std::string DescribeInputLocation(const InputLocation& location);
std::string DescribeLocation(const Location& loc, bool always_show_address);

// If show_path is set, the path to the file will be included, otherwise only
// the last file component will be printed.
std::string DescribeFileLine(const FileLine& file_line, bool show_path = false);

}  // namespace zxdb
