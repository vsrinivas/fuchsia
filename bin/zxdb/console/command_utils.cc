// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include <inttypes.h>
#include <stdio.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

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

Err ReadUint64Arg(const Command& cmd, size_t arg_index,
                  const char* param_desc, uint64_t* out) {
  if (cmd.args().size() <= arg_index) {
    return Err(ErrType::kInput, fxl::StringPrintf(
        "Not enough arguments when reading the %s.", param_desc));
  }
  Err result = StringToUint64(cmd.args()[arg_index], out);
  if (result.has_error()) {
    return Err(ErrType::kInput, fxl::StringPrintf(
        "Invalid number \"%s\" when reading the %s.",
        cmd.args()[arg_index].c_str(), param_desc));
  }
  return Err();
}

std::string DescribeTarget(ConsoleContext* context, Target* target,
                           bool columns) {
  int id = context->IdForTarget(target);

  // Koid string. This includes a trailing space when present so it can be
  // concat'd even when not present and things look nice.
  std::string koid_str;

  const char* state = nullptr;
  switch (target->GetState()) {
    case Target::State::kStopped:
      state = "Stopped";
      break;
    case Target::State::kStarting:
      state = "Starting";
      break;
    case Target::State::kRunning:
      state = "Running";
      koid_str = fxl::StringPrintf(columns ? "%" PRIu64 " "
                                           : "koid=%" PRIu64 " ",
                                   target->GetProcess()->GetKoid());
      break;
  }

  const char* format_string;
  if (columns)
    format_string = "%3d %8s %8s";
  else
    format_string = "Process %d %s %s";
  std::string result = fxl::StringPrintf(format_string, id, state,
                                         koid_str.c_str());

  // When running, use the object name if any.
  std::string name;
  if (target->GetState() == Target::State::kRunning)
    name = target->GetProcess()->GetName();

  // Otherwise fall back to the program name which is the first arg.
  if (name.empty()) {
    const std::vector<std::string>& args = target->GetArgs();
    if (args.empty())
      name += "<no name>";
    else
      name += args[0];
  }
  result += name;

  return result;
}

std::string DescribeThread(ConsoleContext* context, Thread* thread,
                           bool columns) {
  const char* format_string;
  if (columns)
    format_string = "%3d %8" PRIu64 " %s";
  else
    format_string = "Thread %d koid=%" PRIu64 " %s";
  return fxl::StringPrintf(format_string, context->IdForThread(thread),
                           thread->GetKoid(), thread->GetName().c_str());
}

}  // namespace zxdb
