// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_UTILS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_UTILS_H_

#include <initializer_list>
#include <optional>
#include <string>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/eval_callback.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"

namespace zxdb {

class Breakpoint;
class Command;
class ConsoleContext;
class Err;
class ExecutionScope;
class Frame;
class Function;
struct InputLocation;
class Location;
class SymbolServer;
class TargetSymbols;
class Thread;

// Ensures the target is currently running (it has a current Process associated with it. If not,
// generates an error of the form "<command_name> requires a running target".
Err AssertRunningTarget(ConsoleContext* context, const char* command_name, Target* target);

// Validates a command that applies to a stopped thread.
//
// The thread on the command must exist and be stopped.
//
// If validate_nouns is set, only thread and process nouns may be specified (these are most common
// for commands that operate on threads) for the "Thread" variant.
//
// The "Frame" variant also allows a frame override ("frame 3 foo"), always validates nouns, and
// requires that the thread have a currently frame.
//
// If not, generates an error of the form "<command_name> requires a stopped thread".
Err AssertStoppedThreadCommand(ConsoleContext* context, const Command& cmd, bool validate_nouns,
                               const char* command_name);
Err AssertStoppedThreadWithFrameCommand(ConsoleContext* context, const Command& cmd,
                                        const char* command_name);

// Checks if the given string starts with a hexadecimal prefix ("0x" or "0X"). If it does, returns
// the first index into the array of the string FOLLOWING the prefix. If there is no prefix, returns
// 0. If there is only the prefix and nothing following the returned value will be s.size().
size_t CheckHexPrefix(const std::string& s);

[[nodiscard]] Err StringToInt(const std::string& s, int* out);
[[nodiscard]] Err StringToInt64(const std::string& s, int64_t* out);
[[nodiscard]] Err StringToUint32(const std::string& s, uint32_t* out);
[[nodiscard]] Err StringToUint64(const std::string& s, uint64_t* out);

// Reads an int64 from the given index of the command args. Returns an error if there are not enough
// args, or if the value isn't an int64.
//
// The param_desc will be used in the error string, for example "process koid".
[[nodiscard]] Err ReadUint64Arg(const Command& cmd, size_t arg_index, const char* param_desc,
                                uint64_t* out);

std::string ThreadStateToString(debug_ipc::ThreadRecord::State state,
                                debug_ipc::ThreadRecord::BlockedReason blocked_reason);

std::string ExecutionScopeToString(const ConsoleContext* context, const ExecutionScope& scope);

// Converts the command context to an execution scope. This will take the "target"/"thread" if
// explicitly given. If no globa/target/thread context is explicitly given, defaults to the global.
ExecutionScope ExecutionScopeForCommand(const Command& cmd);

std::string BreakpointStopToString(BreakpointSettings::StopMode mode);
const char* BreakpointEnabledToString(bool enabled);

std::string DescribeThread(const ConsoleContext* context, const Thread* thread);

// The |show_context| flag will cause some source code to be included annotated with the breakpoint,
// or a message about pending breakpoints if there is no location.
OutputBuffer FormatBreakpoint(const ConsoleContext* context, const Breakpoint* breakpoint,
                              bool show_context);

OutputBuffer FormatInputLocation(const InputLocation& location);
OutputBuffer FormatInputLocations(const std::vector<InputLocation>& location);

struct FormatLocationOptions {
  // Use the default values.
  FormatLocationOptions() = default;

  // Take the default values from the settings that apply to location formatting. The Target can
  // be null for the default behavior (this simplifies some call sites).
  explicit FormatLocationOptions(const Target* target);

  // How identifier function name formatting should be done.
  FormatFunctionNameOptions func;

  // When set, the address will always be printed. Otherwise it will be omitted if there is a
  // function name present.
  bool always_show_addresses = false;

  // Show function parameter types. Otherwise, it will have "()" (if there are no arguments), or
  // "(...)" if there are some.
  bool show_params = false;

  // Shows file/line information if present.
  bool show_file_line = true;

  // When set forces the file/line (if displayed) to show the full path of the file rather than
  // the shortest possible unique one.
  bool show_file_path = false;

  // Needed when show_file_path is NOT set to shorten paths. This will be used to disambiguate file
  // names. If unset, it will be equivalent to show_file_path = true.
  const TargetSymbols* target_symbols = nullptr;
};

// Formats the location.
OutputBuffer FormatLocation(const Location& loc, const FormatLocationOptions& opts);

// The TargetSymbols pointer is used to find the shortest unique way to reference the file name.
//
// If target_symbols is null, the full file path will always be included.
std::string DescribeFileLine(const TargetSymbols* optional_target_symbols,
                             const FileLine& file_line);

// Returns the best EvalContext for the given command. If there is an available frame, uses that to
// registers and local variables can be read. Otherwise falls back to process (read/write memory and
// globals only) or generic (calculator-like mode only) contexts.
fxl::RefPtr<EvalContext> GetEvalContextForCommand(const Command& cmd);

// Evaluates all args in the given command as an expression and calls the callback with the result.
// The callback will be called from within the stack of the caller if the expression can be
// evaluated synchronously.
//
// When there is an error during setup, the error will be returned and the callback will not be
// called. After setup, all evaluation errors will come via the callback.
//
// The |verb| string is used to format error messages showing command examples.
Err EvalCommandExpression(const Command& cmd, const char* verb,
                          fxl::RefPtr<EvalContext> eval_context, bool follow_references,
                          EvalCallback cb);

// Like EvalCommandExpression but attempts to convert the result to an address. This is used for
// commands that want to support expressions to compute
// addresses.
//
// Some expressions may evaluate to a pointer where the intrinsic size of the pointed-to thing is
// known. In this case, the size will be passed to the callback. Untyped results will have a null
// size.
//
// If the command doesn't evaluate to an address, the Err will be set.
Err EvalCommandAddressExpression(
    const Command& cmd, const char* verb, fxl::RefPtr<EvalContext> eval_context,
    fit::callback<void(const Err& err, uint64_t address, std::optional<uint32_t> size)> cb);

// Formats an argument or setting value.
//
// Normally strings for switches and settings need no quoting since they're whitespace-separated,
// and the input will be returned unchanged.
//
// But if there are spaces or unprintable characters, this will quote or escape in such a way that
// the console/setting formatter will interpret the string the same way as a single entity.
std::string FormatConsoleString(const std::string& input);

// Makes sure there is a runnable target, creating one if necessary. In the success case, the
// returned target should be used instead of the one from the command (it may be a new one).
ErrOr<Target*> GetRunnableTarget(ConsoleContext* context, const Command& cmd);

// Callback for the process/job commands that displays the current process/job and what happened.
// The verb affects the message printed to the screen.
//
// The optional callback parameter will be issued with the error for calling code to identify the
// error.
void ProcessCommandCallback(fxl::WeakPtr<Target> target, bool display_message_on_success,
                            const Err& err, CommandCallback callback);
void JobCommandCallback(const char* verb, fxl::WeakPtr<JobContext> job_context,
                        bool display_message_on_success, const Err& err, CommandCallback callback);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_UTILS_H_
