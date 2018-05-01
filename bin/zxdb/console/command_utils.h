// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>
#include <string>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
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
Err AssertRunningTarget(ConsoleContext* context,
                        const char* command_name,
                        Target* target);

[[nodiscard]] Err StringToUint64(const std::string& s, uint64_t* out);

// Reads an int64 from the given index of the command args. Returns an error
// if there are not enough args, or if the value isn't an int64.
//
// The param_desc will be used in the error string, for example "process koid".
[[nodiscard]] Err ReadUint64Arg(const Command& cmd,
                                size_t arg_index,
                                const char* param_desc,
                                uint64_t* out);

// Parses a host and port. The two-argument version assumes the host and
// port are given separately. The one-argument version assumes they're
// separated by a colon.
Err ParseHostPort(const std::string& in_host,
                  const std::string& in_port,
                  std::string* out_host,
                  uint16_t* out_port);
Err ParseHostPort(const std::string& input,
                  std::string* out_host,
                  uint16_t* out_port);

std::string TargetStateToString(Target::State state);
std::string ThreadStateToString(debug_ipc::ThreadRecord::State state);

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const Breakpoint* breakpoint);
std::string BreakpointStopToString(debug_ipc::Stop stop);

std::string ExceptionTypeToString(debug_ipc::NotifyException::Type type);

// Returns a string describing the given thing in the given context. If
// columns is set, there will be extra padding added so that multiple things
// line up when printed vertically.
std::string DescribeTarget(const ConsoleContext* context,
                           const Target* target,
                           bool columns);
std::string DescribeThread(const ConsoleContext* context,
                           const Thread* thread,
                           bool columns);
std::string DescribeFrame(const Frame* frame, int id);
std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint,
                               bool columns);

enum class Align { kLeft, kRight };

struct ColSpec {
  explicit ColSpec(Align align = Align::kLeft,
                   int max_width = 0,
                   const std::string& head = std::string(),
                   int pad_left = 0)
      : align(align), max_width(max_width), head(head), pad_left(pad_left) {}

  Align align = Align::kLeft;

  // If anything is above the max width, we'll give up and push the remaining
  // cells for that row to the right as necessary.
  //
  // 0 means no maximum.
  int max_width = 0;

  // Empty string means no heading.
  std::string head;

  // Extra padding to the left of this column.
  int pad_left = 0;

  Syntax syntax = Syntax::kNormal;
};

// Formats the given rows in the output as a series of horizontally aligned (if
// possible) columns. The number of columns in the spec vector and in each row
// must match.
void FormatColumns(const std::vector<ColSpec>& spec,
                   const std::vector<std::vector<std::string>>& rows,
                   OutputBuffer* out);

// Helper function to save some typing for static column specs.
inline void FormatColumns(std::initializer_list<ColSpec> spec,
                          const std::vector<std::vector<std::string>>& rows,
                          OutputBuffer* out) {
  return FormatColumns(std::vector<ColSpec>(spec), rows, out);
}

}  // namespace zxdb
