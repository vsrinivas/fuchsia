// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CONSOLE_COMMAND_UTILS_H_
#define GARNET_BIN_ZXDB_CONSOLE_COMMAND_UTILS_H_

#include <initializer_list>
#include <optional>
#include <string>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/expr_eval_context.h"

namespace zxdb {

class Breakpoint;
class Command;
class ConsoleContext;
class Err;
class Frame;
class Function;
struct InputLocation;
class Location;
class SymbolServer;
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

// Checks if the given string starts with a hexadecimal prefix ("0x" or "0X").
// If it does, returns the first index into the array of the string FOLLOWING
// the prefix. If there is no prefix, returns 0. If there is only the prefix
// and nothing following the returned value will be s.size().
size_t CheckHexPrefix(const std::string& s);

[[nodiscard]] Err StringToInt(const std::string& s, int* out);
[[nodiscard]] Err StringToInt64(const std::string& s, int64_t* out);
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
std::string JobContextStateToString(JobContext::State state);
std::string ThreadStateToString(
    debug_ipc::ThreadRecord::State state,
    debug_ipc::ThreadRecord::BlockedReason blocked_reason);

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const BreakpointSettings& settings);
std::string BreakpointStopToString(BreakpointSettings::StopMode mode);
const char* BreakpointEnabledToString(bool enabled);
const char* BreakpointTypeToString(debug_ipc::BreakpointType);

std::string DescribeTarget(const ConsoleContext* context, const Target* target);

std::string DescribeJobContext(const ConsoleContext* context,
                               const JobContext* job_context);

// Returns the process name of the given target, depending on the running
// process or the current app name, as applicable.
std::string DescribeTargetName(const Target* target);

// Returns the job name of the given job context.
std::string DescribeJobContextName(const JobContext* job_context);

std::string DescribeThread(const ConsoleContext* context, const Thread* thread);

std::string DescribeSymbolServer(const ConsoleContext* context,
                                 const SymbolServer* symbol_server);

std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint);

std::string DescribeInputLocation(const InputLocation& location);

// Formats the given string as an identifier, with any template annotations
// dimmed. If bold_last is set, the last identifier component will be bolded.
OutputBuffer FormatIdentifier(const std::string& str, bool bold_last);

// Formats the function name with syntax highlighting. If show_params is true,
// the types of the function parameters will be output. Otherwise the
// function name will end with "()" if there are no parameters, or "(...)" if
// there are some. The goal is to be as short as possible without being
// misleading (showing "()" when there are parameters may be misleading, and
// no parens at all don't look like a function).
OutputBuffer FormatFunctionName(const Function* function, bool show_params);

// Formats the location. Normally if a function name is present the code
// address will be omitted, but always_show_address will override this.
OutputBuffer FormatLocation(const Location& loc, bool always_show_address,
                            bool show_params);

// If show_path is set, the path to the file will be included, otherwise only
// the last file component will be printed.
std::string DescribeFileLine(const FileLine& file_line, bool show_path = false);

// The setting "set" command has different modification modes, which depend on
// the setting type being modified.
enum class AssignType {
  kAssign,  // =    Sets a complete value for the setting.
  kAppend,  // +=   Appends values to the setting (list only).
  kRemove,  // -=   Removes values from the list (list only).
};
const char* AssignTypeToString(AssignType);

// Parse the arguments for the set command and find out which assignment
// operation it is and what are the actual elements to set.
Err SetElementsToAdd(const std::vector<std::string>& args,
                     AssignType* assign_type,
                     std::vector<std::string>* elements_to_set);

// Returns the best ExprEvalContext for the given command. If there is an
// available frame, uses that to registers and local variables can be read.
// Otherwise falls back to process (read/write memory and globals only) or
// generic (calculator-like mode only) contexts.
fxl::RefPtr<ExprEvalContext> GetEvalContextForCommand(const Command& cmd);

// Evaluates all args in the given command as an expression and calls the
// callback with the result. The callback will be called from within the
// stack of the caller if the expression can be evaluated synchronously.
//
// When there is an error during setup, the error will be returned and the
// callback will not be called. After setup, all evaluation errors will come
// via the callback.
//
// The |verb| string is used to format error messages showing command examples.
Err EvalCommandExpression(
    const Command& cmd, const char* verb,
    fxl::RefPtr<ExprEvalContext> eval_context, bool follow_references,
    std::function<void(const Err& err, ExprValue value)> cb);

// Like EvalCommandExpression but attempts to convert the result to an address.
// This is used for commands that want to support expressions to compute
// addresses.
//
// Some expressions may evaluate to a pointer where the intrinsic size of the
// pointed-to thing is known. In this case, the size will be passed to the
// callback. Untyped results will have a null size.
//
// If the command doesn't evaluate to an address, the Err will be set.
Err EvalCommandAddressExpression(
    const Command& cmd, const char* verb,
    fxl::RefPtr<ExprEvalContext> eval_context,
    std::function<void(const Err& err, uint64_t address,
                       std::optional<uint32_t> size)>
        cb);

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CONSOLE_COMMAND_UTILS_H_
