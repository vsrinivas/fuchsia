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
#include "src/developer/debug/zxdb/client/function_return_info.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/console/command.h"
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

// Validates a command that applies to a single stopped thread with a valid frame.
//
// If validate_nouns is set, only thread and process nouns may be specified (these are most common
// for commands that operate on threads) for the "Thread" variant.
//
// If not, generates an error of the form "<command_name> requires a stopped thread".
Err AssertStoppedThreadWithFrameCommand(ConsoleContext* context, const Command& cmd,
                                        const char* command_name, bool validate_nounds = true);

// Asserts that all threads of the process for the given command are stopped. This does not check if
// the frames have full stacks.
Err AssertAllStoppedThreadsCommand(ConsoleContext* context, const Command& cmd,
                                   const char* command_name, bool validate_nounds = true);

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

std::string ThreadStateToString(std::optional<debug_ipc::ThreadRecord::State> state,
                                debug_ipc::ThreadRecord::BlockedReason blocked_reason);

std::string ExecutionScopeToString(const ConsoleContext* context, const ExecutionScope& scope);

// Converts the command context to an execution scope. This will take the "target"/"thread" if
// explicitly given. If no globa/target/thread context is explicitly given, defaults to the global.
ExecutionScope ExecutionScopeForCommand(const Command& cmd);

// Find breakpoints to modify. |cmd| is enable/disable/clear with an optional location.
// If a location is given, returns all breakpoints at that location.
// If no location is provided, returns current active breakpoint, which could be affected
// by prefixing "bp <index>" before the command.
Err ResolveBreakpointsForModification(const Command& cmd, const char* command_name,
                                      std::vector<Breakpoint*>* output);

OutputBuffer FormatThread(const ConsoleContext* context, const Thread* thread);

// The |show_context| flag will cause some source code to be included annotated with the breakpoint,
// or a message about pending breakpoints if there is no location.
OutputBuffer FormatBreakpoint(const ConsoleContext* context, const Breakpoint* breakpoint,
                              bool show_context);

OutputBuffer FormatInputLocation(const InputLocation& location);
OutputBuffer FormatInputLocations(const std::vector<InputLocation>& location);

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
// The verbose_errors flag, if set, will wrap any expression evaluation errors with some
// explanation that the expression has failed to evaluate. Most callers except "print" will want
// verbose errors because short things like "Optimized out" make sense in the context of printing
// a value, but not for e.g. the result of "watch foo".
//
// The |verb| string is used to format error messages showing command examples.
Err EvalCommandExpression(const Command& cmd, const char* verb,
                          const fxl::RefPtr<EvalContext>& eval_context, bool follow_references,
                          bool verbose_errors, EvalCallback cb);

// Like EvalCommandExpression but attempts to convert the result to an address. This is used for
// commands that want to support expressions to compute addresses.
//
// Some expressions may evaluate to a pointer where the intrinsic size of the pointed-to thing is
// known. In this case, the size will be passed to the callback. Untyped results will have a null
// size.
//
// If the command doesn't evaluate to an address, the Err will be set.
Err EvalCommandAddressExpression(
    const Command& cmd, const char* verb, const fxl::RefPtr<EvalContext>& eval_context,
    fit::callback<void(const Err& err, uint64_t address, std::optional<uint32_t> size)> cb);

// Errors from the evaluation of expressions of commands often don't make sense without context.
// This function wraps the given error in a message explaining it's from evaluating an expression
// for the given verb. If the verb string is empty, it will be a generic command error.
Err RewriteCommandExpressionError(const std::string& verb, const Err& err);

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

// If the system has at least one running process, returns no error. If not, returns an error
// describing that there must be a process running.
//
// When doing global things like System::Continue(), it will succeed if there are no running
// programs (it will successfully continue all 0 processes). This is confusing to the user so this
// function is used to check first.
Err VerifySystemHasRunningProcess(System* system);

// Callback for the process commands that displays the current process and what happened.
// The verb affects the message printed to the screen.
//
// The optional callback parameter will be issued with the error for calling code to identify the
// error.
void ProcessCommandCallback(fxl::WeakPtr<Target> target, bool display_message_on_success,
                            const Err& err, fxl::RefPtr<CommandContext> cmd_context);

// Schedules the function's return information to be printed from a PostStopTask on the thread
// (the thread is in the FunctionReturnInfo).
//
// This must only be called from a ThreadController::OnThreadStop handler: in normal use this
// callback will be given to a thread controller to issue when a function return happens.
//
// The PostStopTask that this function schedules will evaluate the return value, print it, and then
// notify the thread that it can resume its normal behavior (either a stop or a continue).
//
// If this function returns void or there's an error, this does nothing.
void ScheduleAsyncPrintReturnValue(const FunctionReturnInfo& info);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMAND_UTILS_H_
