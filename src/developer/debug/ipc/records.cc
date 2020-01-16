// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/records.h"

#include <algorithm>

#include "src/lib/fxl/logging.h"

namespace debug_ipc {

bool IsDebug(ExceptionType type) {
  switch (type) {
    // There's an argument to be had about whether these belong here.
    case ExceptionType::kThreadStarting:
    case ExceptionType::kThreadExiting:
    case ExceptionType::kProcessStarting:

    case ExceptionType::kHardware:
    case ExceptionType::kWatchpoint:
    case ExceptionType::kSingleStep:
    case ExceptionType::kSoftware:
    case ExceptionType::kSynthetic:
      return true;
    default:
      return false;
  }
}

__int128 Register::GetValue() const {
  __int128 result = 0;
  if (!data.empty())
    memcpy(&result, &data[0], std::min(sizeof(result), data.size()));
  return result;
}

const char* ExceptionTypeToString(ExceptionType type) {
  switch (type) {
    case ExceptionType::kNone:
      return "None";
    case ExceptionType::kGeneral:
      return "General";
    case ExceptionType::kPageFault:
      return "Fatal Page Fault";
    case ExceptionType::kUndefinedInstruction:
      return "Undefined Instruction";
    case ExceptionType::kUnalignedAccess:
      return "Unaligned Access";
    case ExceptionType::kPolicyError:
      return "Policy Error";
    case ExceptionType::kThreadStarting:
      return "Thread Starting";
    case ExceptionType::kThreadExiting:
      return "Thread Exiting";
    case ExceptionType::kProcessStarting:
      return "Process Starting";
    case ExceptionType::kHardware:
      return "Hardware";
    case ExceptionType::kWatchpoint:
      return "Watchpoint";
    case ExceptionType::kSingleStep:
      return "Single Step";
    case ExceptionType::kSoftware:
      return "Software";
    case ExceptionType::kSynthetic:
      return "Synthetic";
    case ExceptionType::kUnknown:
      return "Unknown";
    case ExceptionType::kLast:
      return "kLast";
  }
  FXL_NOTREACHED();
  return nullptr;
}

const char* ThreadRecord::StateToString(ThreadRecord::State state) {
  switch (state) {
    case ThreadRecord::State::kNew:
      return "New";
    case ThreadRecord::State::kRunning:
      return "Running";
    case ThreadRecord::State::kSuspended:
      return "Suspended";
    case ThreadRecord::State::kBlocked:
      return "Blocked";
    case ThreadRecord::State::kDying:
      return "Dying";
    case ThreadRecord::State::kDead:
      return "Dead";
    case ThreadRecord::State::kCoreDump:
      return "Core Dump";
    case ThreadRecord::State::kLast:
      break;
  }

  FXL_NOTREACHED();
  return "";
}

const char* ThreadRecord::BlockedReasonToString(BlockedReason reason) {
  switch (reason) {
    case ThreadRecord::BlockedReason::kNotBlocked:
      return "Not blocked";
    case ThreadRecord::BlockedReason::kException:
      return "Exception";
    case ThreadRecord::BlockedReason::kSleeping:
      return "Sleeping";
    case ThreadRecord::BlockedReason::kFutex:
      return "Futex";
    case ThreadRecord::BlockedReason::kPort:
      return "Port";
    case ThreadRecord::BlockedReason::kChannel:
      return "Channel";
    case ThreadRecord::BlockedReason::kWaitOne:
      return "Wait one";
    case ThreadRecord::BlockedReason::kWaitMany:
      return "Wait many";
    case ThreadRecord::BlockedReason::kInterrupt:
      return "Interrupt";
    case ThreadRecord::BlockedReason::kLast:
      break;
  }

  FXL_NOTREACHED();
  return "";
}

const char* BreakpointTypeToString(BreakpointType type) {
  switch (type) {
    case BreakpointType::kSoftware:
      return "Software";
    case BreakpointType::kHardware:
      return "Hardware";
    case BreakpointType::kReadWrite:
      return "ReadWrite";
    case BreakpointType::kWrite:
      return "Write";
    case BreakpointType::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return nullptr;
}

bool IsWatchpointType(debug_ipc::BreakpointType type) {
  // clang-format off
  return type == BreakpointType::kReadWrite ||
         type == BreakpointType::kWrite;
  // clang-format on
}

const char* ConfigAction::TypeToString(Type type) {
  switch (type) {
    case Type::kQuitOnExit:
      return "Quit On Exit";
    case Type::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return nullptr;
}

}  // namespace debug_ipc
