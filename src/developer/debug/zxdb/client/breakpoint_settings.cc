// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint_settings.h"

#include "src/developer/debug/ipc/protocol.h"
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

// static
Err BreakpointSettings::ValidateSize(debug_ipc::Arch arch, Type type, uint32_t byte_size) {
  // Note that "arch" may be kUnknown at this point if the user is making a breakpoint before
  // connecting. That should be OK and weaker validation should be done.
  if (!TypeHasSize(type)) {
    if (byte_size != 0) {
      return Err("Breakpoints of type '%s' don't have sizes associated with them.",
                 TypeToString(type));
    }
    return Err();
  }

  // All hardware breakpoints have a size.
  if (arch == debug_ipc::Arch::kX64 && type == Type::kHardware) {
    // x64 only supports 1-byte hardware execution breakpoints.
    if (byte_size != 1)
      return Err("Intel CPUs only support hardware execution breakpoints of 1 byte.");
    return Err();
  }

  // Our backend on all platforms currently supports only 1, 2, 4, and 8 byte hardware breakpoints
  // for all other cases.
  if (byte_size != 1 && byte_size != 2 && byte_size != 4 && byte_size != 8) {
    return Err(
        "Hardware breakpoints must be 1, 2, 4, or 8 bytes long only. If you need a\n"
        "slightly longer one, you can create several adjacent 8-byte ones, but there\n"
        "are a limited number of hardware breakpoints supported by the CPU.");
  }

  return Err();
}

}  // namespace zxdb
