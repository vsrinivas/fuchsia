// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Command;
class ConsoleContext;
class Err;
class Frame;
class Thread;

// Ensures the target is currently running (it has a current Process associated
// with it. If not, generates an error of the form
// "<command_name> requires a running target".
Err AssertRunningTarget(ConsoleContext* context, const char* command_name,
                        Target* target);

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
Err ParseHostPort(const std::string& input,
                  std::string* out_host, uint16_t* out_port);

std::string TargetStateToString(Target::State state);
std::string ThreadStateToString(debug_ipc::ThreadRecord::State state);

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const Breakpoint* breakpoint);
std::string BreakpointStopToString(debug_ipc::Stop stop);

std::string ExceptionTypeToString(debug_ipc::NotifyException::Type type);

// Returns a string describing the given thing in the given context. If
// columns is set, there will be extra padding added so that multiple things
// line up when printed vertically.
std::string DescribeTarget(const ConsoleContext* context, const Target* target,
                           bool columns);
std::string DescribeThread(const ConsoleContext* context, const Thread* thread,
                           bool columns);
std::string DescribeFrame(const Frame* frame, int id);
std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint,
                               bool columns);

}  // namespace zxdb
