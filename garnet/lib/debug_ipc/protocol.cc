// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/protocol.h"

#include "lib/fxl/logging.h"

namespace debug_ipc {

constexpr uint32_t MsgHeader::kSerializedHeaderSize;

constexpr uint64_t HelloReply::kStreamSignature;
constexpr uint32_t HelloReply::kCurrentVersion;

const char* MsgHeader::TypeToString(MsgHeader::Type type) {
  switch (type) {
    case MsgHeader::Type::kNone:
      return "None";
    case MsgHeader::Type::kHello:
      return "Hello";
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
    case MsgHeader::Type::kSymbolTables:
      return "SymbolTables";
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
    case MsgHeader::Type::kNumMessages:
      return "NumMessages";
  }

  FXL_NOTREACHED();
  return "";
}

const char* NotifyException::TypeToString(NotifyException::Type type) {
  switch (type) {
    case NotifyException::Type::kNone:
      return "None";
    case NotifyException::Type::kGeneral:
      return "General";
    case NotifyException::Type::kHardware:
      return "Hardware";
    case NotifyException::Type::kSingleStep:
      return "Single Step";
    case NotifyException::Type::kSoftware:
      return "Software";
    case NotifyException::Type::kLast:
      break;
  }
  FXL_NOTREACHED();
  return "";
}

}  // namespace debug_ipc
