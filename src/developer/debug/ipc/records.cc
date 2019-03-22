// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/records.h"

#include "lib/fxl/logging.h"

namespace debug_ipc {

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

const char* RegisterCategory::TypeToString(RegisterCategory::Type type) {
  switch (type) {
    case RegisterCategory::Type::kGeneral:
      return "General Purpose";
    case RegisterCategory::Type::kFP:
      return "Floating Point";
    case RegisterCategory::Type::kVector:
      return "Vector";
    case RegisterCategory::Type::kDebug:
      return "Debug";
    case RegisterCategory::Type::kNone:
      break;
  }
  FXL_NOTREACHED();
  return nullptr;
}

RegisterCategory::Type RegisterCategory::RegisterIDToCategory(RegisterID id) {
  uint32_t val = static_cast<uint32_t>(id);

  // ARM.
  if (val >= kARMv8GeneralBegin && val <= kARMv8GeneralEnd) {
    return RegisterCategory::Type::kGeneral;
  } else if (val >= kARMv8VectorBegin && val <= kARMv8VectorEnd) {
    return RegisterCategory::Type::kVector;
  } else if (val >= kARMv8DebugBegin && val <= kARMv8DebugEnd) {
    return RegisterCategory::Type::kDebug;
  }

  // x64.
  if (val >= kX64GeneralBegin && val <= kX64GeneralEnd) {
    return RegisterCategory::Type::kGeneral;
  } else if (val >= kX64FPBegin && val <= kX64FPEnd) {
    return RegisterCategory::Type::kFP;
  } else if (val >= kX64VectorBegin && val <= kX64VectorEnd) {
    return RegisterCategory::Type::kVector;
  } else if (val >= kX64DebugBegin && val <= kX64DebugEnd) {
    return RegisterCategory::Type::kDebug;
  }

  return RegisterCategory::Type::kNone;
}

}  // namespace debug_ipc
