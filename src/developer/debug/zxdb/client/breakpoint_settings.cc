// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint_settings.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"

namespace zxdb {

// static
const char* BreakpointSettings::StopModeToString(StopMode stop_mode) {
  switch (stop_mode) {
    case BreakpointSettings::StopMode::kNone:
      return ClientSettings::Breakpoint::kStopMode_None;
    case BreakpointSettings::StopMode::kThread:
      return ClientSettings::Breakpoint::kStopMode_Thread;
    case BreakpointSettings::StopMode::kProcess:
      return ClientSettings::Breakpoint::kStopMode_Process;
    case BreakpointSettings::StopMode::kAll:
      return ClientSettings::Breakpoint::kStopMode_All;
  }
  return "<invalid>";
}

// static
std::optional<BreakpointSettings::StopMode> BreakpointSettings::StringToStopMode(
    std::string_view value) {
  if (value == ClientSettings::Breakpoint::kStopMode_None) {
    return BreakpointSettings::StopMode::kNone;
  } else if (value == ClientSettings::Breakpoint::kStopMode_Thread) {
    return BreakpointSettings::StopMode::kThread;
  } else if (value == ClientSettings::Breakpoint::kStopMode_Process) {
    return BreakpointSettings::StopMode::kProcess;
  } else if (value == ClientSettings::Breakpoint::kStopMode_All) {
    return BreakpointSettings::StopMode::kAll;
  }
  return std::nullopt;
}

// static
const char* BreakpointSettings::TypeToString(BreakpointSettings::Type t) {
  switch (t) {
    case BreakpointSettings::Type::kSoftware:
      return ClientSettings::Breakpoint::kType_Software;
    case BreakpointSettings::Type::kHardware:
      return ClientSettings::Breakpoint::kType_Hardware;
    case BreakpointSettings::Type::kReadWrite:
      return ClientSettings::Breakpoint::kType_ReadWrite;
    case BreakpointSettings::Type::kWrite:
      return ClientSettings::Breakpoint::kType_Write;
    case BreakpointSettings::Type::kLast:
      break;  // Not valid.
  }
  FXL_NOTREACHED();
  return "<invalid>";
}

// static
std::optional<BreakpointSettings::Type> BreakpointSettings::StringToType(std::string_view value) {
  if (value == ClientSettings::Breakpoint::kType_Software) {
    return BreakpointSettings::Type::kSoftware;
  } else if (value == ClientSettings::Breakpoint::kType_Hardware) {
    return BreakpointSettings::Type::kHardware;
  } else if (value == ClientSettings::Breakpoint::kType_ReadWrite) {
    return BreakpointSettings::Type::kReadWrite;
  } else if (value == ClientSettings::Breakpoint::kType_Write) {
    return BreakpointSettings::Type::kWrite;
  }
  return std::nullopt;
}

// static
bool BreakpointSettings::TypeHasSize(Type t) { return t == Type::kReadWrite || t == Type::kWrite; }

}  // namespace zxdb
