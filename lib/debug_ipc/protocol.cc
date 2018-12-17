// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

constexpr uint32_t MsgHeader::kSerializedHeaderSize;

constexpr uint64_t HelloReply::kStreamSignature;
constexpr uint32_t HelloReply::kCurrentVersion;

const char* MsgHeaderTypeToString(MsgHeader::Type type) {
  switch (type) {
    case MsgHeader::Type::kNone:
      return "kNone";
    case MsgHeader::Type::kHello:
      return "kHello";
    case MsgHeader::Type::kLaunch:
      return "kLaunch";
    case MsgHeader::Type::kKill:
      return "kKill";
    case MsgHeader::Type::kAttach:
      return "kAttach";
    case MsgHeader::Type::kDetach:
      return "kDetach";
    case MsgHeader::Type::kModules:
      return "kModules";
    case MsgHeader::Type::kPause:
      return "kPause";
    case MsgHeader::Type::kQuitAgent:
      return "kQuitAgent";
    case MsgHeader::Type::kResume:
      return "kResume";
    case MsgHeader::Type::kProcessTree:
      return "kProcessTree";
    case MsgHeader::Type::kThreads:
      return "kThreads";
    case MsgHeader::Type::kReadMemory:
      return "kReadMemory";
    case MsgHeader::Type::kWriteMemory:
      return "kWriteMemory";
    case MsgHeader::Type::kRegisters:
      return "kRegisters";
    case MsgHeader::Type::kAddOrChangeBreakpoint:
      return "kAddOrChangeBreakpoint";
    case MsgHeader::Type::kRemoveBreakpoint:
      return "kRemoveBreakpoint";
    case MsgHeader::Type::kThreadStatus:
      return "kThreadStatus";
    case MsgHeader::Type::kAddressSpace:
      return "kAddressSpace";
    case MsgHeader::Type::kJobFilter:
      return "kJobFilter";
    case MsgHeader::Type::kNotifyProcessExiting:
      return "kNotifyProcessExiting";
    case MsgHeader::Type::kNotifyProcessStarting:
      return "kNotifyProcessStarting";
    case MsgHeader::Type::kNotifyThreadStarting:
      return "kNotifyThreadStarting";
    case MsgHeader::Type::kNotifyThreadExiting:
      return "kNotifyThreadExiting";
    case MsgHeader::Type::kNotifyException:
      return "kNotifyException";
    case MsgHeader::Type::kNotifyModules:
      return "kNotifyModules";
    case MsgHeader::Type::kNumMessages:
      return "kNumMessages";
  }
}

}  // namespace debug_ipc
