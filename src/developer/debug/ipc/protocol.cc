// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/protocol.h"

#include "src/lib/fxl/logging.h"

#if defined(__Fuchsia__)

#include <zircon/types.h>

// If this static assertion fails, then we have to redefine
// debug_ipc::zx_status_t appropriately.
static_assert(std::is_same<::zx_status_t, debug_ipc::zx_status_t>::value);

#endif

namespace debug_ipc {

constexpr uint32_t MsgHeader::kSerializedHeaderSize;

constexpr uint64_t HelloReply::kStreamSignature;
constexpr uint32_t HelloReply::kCurrentVersion;

const char* MsgHeader::TypeToString(MsgHeader::Type type) {
  switch (type) {
    case MsgHeader::Type::kConfigAgent:
      return "ConfigAgent";
    case MsgHeader::Type::kNone:
      return "None";
    case MsgHeader::Type::kHello:
      return "Hello";
    case MsgHeader::Type::kStatus:
      return "Status";
    case MsgHeader::Type::kLaunch:
      return "Launch";
    case MsgHeader::Type::kKill:
      return "Kill";
    case MsgHeader::Type::kAttach:
      return "Attach";
    case MsgHeader::Type::kDetach:
      return "Detach";
    case MsgHeader::Type::kModules:
      return "Modules";
    case MsgHeader::Type::kPause:
      return "Pause";
    case MsgHeader::Type::kQuitAgent:
      return "QuitAgent";
    case MsgHeader::Type::kResume:
      return "Resume";
    case MsgHeader::Type::kProcessTree:
      return "ProcessTree";
    case MsgHeader::Type::kThreads:
      return "Threads";
    case MsgHeader::Type::kReadMemory:
      return "ReadMemory";
    case MsgHeader::Type::kWriteMemory:
      return "WriteMemory";
    case MsgHeader::Type::kReadRegisters:
      return "ReadRegisters";
    case MsgHeader::Type::kWriteRegisters:
      return "WriteRegisters";
    case MsgHeader::Type::kAddOrChangeBreakpoint:
      return "AddOrChangeBreakpoint";
    case MsgHeader::Type::kRemoveBreakpoint:
      return "RemoveBreakpoint";
    case MsgHeader::Type::kSysInfo:
      return "SysInfo";
    case MsgHeader::Type::kProcessStatus:
      return "ProcessStatus";
    case MsgHeader::Type::kThreadStatus:
      return "ThreadStatus";
    case MsgHeader::Type::kAddressSpace:
      return "AddressSpace";
    case MsgHeader::Type::kJobFilter:
      return "JobFilter";
    case MsgHeader::Type::kNotifyProcessExiting:
      return "NotifyProcessExiting";
    case MsgHeader::Type::kNotifyProcessStarting:
      return "NotifyProcessStarting";
    case MsgHeader::Type::kNotifyThreadStarting:
      return "NotifyThreadStarting";
    case MsgHeader::Type::kNotifyThreadExiting:
      return "NotifyThreadExiting";
    case MsgHeader::Type::kNotifyException:
      return "NotifyException";
    case MsgHeader::Type::kNotifyModules:
      return "NotifyModules";
    case MsgHeader::Type::kNotifyIO:
      return "NotifyIO";
    case MsgHeader::Type::kNumMessages:
      return "NumMessages";
  }

  FXL_NOTREACHED();
  return "<invalid>";
}

const char* InferiorTypeToString(InferiorType type) {
  switch (type) {
    case InferiorType::kBinary:
      return "kBinary";
    case InferiorType::kComponent:
      return "kComponent";
    case InferiorType::kLast:
      return "kLast";
  }

  FXL_NOTREACHED();
  return "<invalid>";
}

const char* TaskTypeToString(TaskType type) {
  switch (type) {
    case TaskType::kProcess:
      return "Process";
    case TaskType::kJob:
      return "Job";
    case TaskType::kSystemRoot:
      return "System root";
    case TaskType::kComponentRoot:
      return "Component Root";
    case TaskType::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return "<invalid>";
}

const char* NotifyIO::TypeToString(Type type) {
  switch (type) {
    case Type::kStderr:
      return "Stderr";
    case Type::kStdout:
      return "Stdout";
    case Type::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return "<invalid>";
}

const char* ResumeRequest::HowToString(How how) {
  switch (how) {
    case How::kContinue:
      return "Continue";
    case How::kStepInstruction:
      return "Step Instruction";
    case How::kStepInRange:
      return "Step In Range";
    case How::kLast:
      return "Last";
  }

  FXL_NOTREACHED();
  return "<unknown>";
}

}  // namespace debug_ipc
