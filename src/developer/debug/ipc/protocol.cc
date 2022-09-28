// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/protocol.h"

#include <lib/syslog/cpp/macros.h>

namespace debug_ipc {

constexpr uint32_t MsgHeader::kSerializedHeaderSize;

constexpr uint64_t HelloReply::kStreamSignature;

const char* MsgHeader::TypeToString(MsgHeader::Type type) {
  switch (type) {
#define FN(type, ...)            \
  case MsgHeader::Type::k##type: \
    return #type;
    FOR_EACH_REQUEST_TYPE(FN)
    FOR_EACH_NOTIFICATION_TYPE(FN)
#undef FN

    case Type::kNone:
      return "None";
    case Type::kNumMessages:
      return "NumMessages";
  }
}

const char* InferiorTypeToString(InferiorType type) {
  switch (type) {
    case InferiorType::kBinary:
      return "Binary";
    case InferiorType::kComponent:
      return "Component";
    case InferiorType::kTest:
      return "Test";
    case InferiorType::kLast:
      return "Last";
  }

  FX_NOTREACHED();
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

  FX_NOTREACHED();
  return "<invalid>";
}

const char* ResumeRequest::HowToString(How how) {
  switch (how) {
    case How::kResolveAndContinue:
      return "Resolve and Continue";
    case How::kForwardAndContinue:
      return "Forward and Continue";
    case How::kStepInstruction:
      return "Step Instruction";
    case How::kStepInRange:
      return "Step In Range";
    case How::kLast:
      return "Last";
  }

  FX_NOTREACHED();
  return "<unknown>";
}

const char* NotifyProcessStarting::TypeToString(Type type) {
  // clang-format off
  switch (type) {
    case Type::kAttach: return "Attach";
    case Type::kLaunch: return "Launch";
    case Type::kLimbo: return "Limbo";
    case Type::kLast: return "<last>";
  }
  // clang-format on

  FX_NOTREACHED();
  return "<unknown>";
}

}  // namespace debug_ipc
