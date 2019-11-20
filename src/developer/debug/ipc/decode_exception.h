// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_DECODE_EXCEPTION_H_
#define SRC_DEVELOPER_DEBUG_IPC_DECODE_EXCEPTION_H_

#include <stdint.h>

#include <optional>

#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {

class Arm64ExceptionInfo {
 public:
  // Get the value of the ESR register. A nullopt indicates failure.
  virtual std::optional<uint32_t> FetchESR() = 0;
};

class X64ExceptionInfo {
 public:
  struct DebugRegs {
    uint64_t dr0 = 0;
    uint64_t dr1 = 0;
    uint64_t dr2 = 0;
    uint64_t dr3 = 0;
    uint64_t dr6 = 0;
    uint64_t dr7 = 0;
  };

  // Get the necessary debug registers for decoding exceptions. A nullopt indicates failure.
  virtual std::optional<DebugRegs> FetchDebugRegs() = 0;
  virtual void ClearDebugFlags(const DebugRegs&) = 0;
};

ExceptionType DecodeException(uint32_t code, X64ExceptionInfo* info);
ExceptionType DecodeException(uint32_t code, Arm64ExceptionInfo* info);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_DECODE_EXCEPTION_H_
