// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint.h"

#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

const char* ClientSettings::Breakpoint::kLocation = "location";
const char* ClientSettings::Breakpoint::kLocationDescription =
    R"(  The location (symbol, line number, address, or expression) where this
  breakpoint will be set. See "help break" for documentation on how to specify.)";

const char* ClientSettings::Breakpoint::kScope = "scope";
const char* ClientSettings::Breakpoint::kScopeDescription =
    R"(  What this breakpoint applies to. Examples:

    global:     All processes (the default).
    "pr 3":     All threads in a process 3.
    "pr 3 t 2": Only thread 2 of process 3.)";

const char* ClientSettings::Breakpoint::kEnabled = "enabled";
const char* ClientSettings::Breakpoint::kEnabledDescription =
    R"(  Whether this breakpoint is enabled. Disabled breakpoints keep their settings
  but are not installed and will not stop or increment their hit count.)";

const char* ClientSettings::Breakpoint::kOneShot = "one-shot";
const char* ClientSettings::Breakpoint::kOneShotDescription =
    R"(  Whether this breakpoint is one-shot. One-shot breakpoints are automatically
  deleted when hit.)";

const char* ClientSettings::Breakpoint::kType = "type";
const char* ClientSettings::Breakpoint::kTypeDescription =
    "  Type of breakpoint. Possible values are:\n\n" BREAKPOINT_TYPE_HELP("    ");

const char* ClientSettings::Breakpoint::kType_Software = "software";
const char* ClientSettings::Breakpoint::kType_Hardware = "execute";
const char* ClientSettings::Breakpoint::kType_ReadWrite = "read-write";
const char* ClientSettings::Breakpoint::kType_Write = "write";

const char* ClientSettings::Breakpoint::kSize = "size";
const char* ClientSettings::Breakpoint::kSizeDescription =
    R"(  Byte size for hardware breakpoints.

  Hardware "write" and "read-write" breakpoints can be set on a range of
  addresses. The supported ranges are architecture-specific, but sizes of 1, 2,
  4 and 8 bytes should be supported. The address will need to be aligned
  to an even multiple of its size.)";

const char* ClientSettings::Breakpoint::kStopMode = "stop";
const char* ClientSettings::Breakpoint::kStopModeDescription =
    R"(  What to stop when this breakpoint is hit. Possible values are:

  none
      Do not stop anything when this breakpoint is hit. The breakpoint will
      still be installed and will still accumulate hit counts.

  thread
      Stop only the thread that hit the breakpoint. Other threads in the same
      process and other processes will be unaffected.

  process
      Stop all threads in the process that hit the breakpoint. Other processes
      being debugged will be unaffected.

  all
      Stop all processes currently being debugged.)";
const char* ClientSettings::Breakpoint::kStopMode_None = "none";
const char* ClientSettings::Breakpoint::kStopMode_Thread = "thread";
const char* ClientSettings::Breakpoint::kStopMode_Process = "process";
const char* ClientSettings::Breakpoint::kStopMode_All = "all";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  schema->AddInputLocations(ClientSettings::Breakpoint::kLocation,
                            ClientSettings::Breakpoint::kLocationDescription);
  schema->AddExecutionScope(ClientSettings::Breakpoint::kScope,
                            ClientSettings::Breakpoint::kScopeDescription);
  schema->AddBool(ClientSettings::Breakpoint::kEnabled,
                  ClientSettings::Breakpoint::kEnabledDescription, true);
  schema->AddBool(ClientSettings::Breakpoint::kOneShot,
                  ClientSettings::Breakpoint::kOneShotDescription, false);
  schema->AddString(
      ClientSettings::Breakpoint::kType, ClientSettings::Breakpoint::kTypeDescription,
      ClientSettings::Breakpoint::kType_Software,
      {ClientSettings::Breakpoint::kType_Software, ClientSettings::Breakpoint::kType_Hardware,
       ClientSettings::Breakpoint::kType_ReadWrite, ClientSettings::Breakpoint::kType_Write});
  schema->AddInt(ClientSettings::Breakpoint::kSize, ClientSettings::Breakpoint::kSizeDescription);
  schema->AddString(
      ClientSettings::Breakpoint::kStopMode, ClientSettings::Breakpoint::kStopModeDescription,
      ClientSettings::Breakpoint::kStopMode_All,
      {ClientSettings::Breakpoint::kStopMode_None, ClientSettings::Breakpoint::kStopMode_Thread,
       ClientSettings::Breakpoint::kStopMode_Process, ClientSettings::Breakpoint::kStopMode_All});
  return schema;
}

}  // namespace

Breakpoint::Settings::Settings(Breakpoint* bp) : SettingStore(Breakpoint::GetSchema()), bp_(bp) {}

SettingValue Breakpoint::Settings::GetStorageValue(const std::string& key) const {
  BreakpointSettings settings = bp_->GetSettings();
  if (key == ClientSettings::Breakpoint::kLocation) {
    return SettingValue(settings.locations);
  } else if (key == ClientSettings::Breakpoint::kScope) {
    return SettingValue(settings.scope);
  } else if (key == ClientSettings::Breakpoint::kStopMode) {
    return SettingValue(BreakpointSettings::StopModeToString(settings.stop_mode));
  } else if (key == ClientSettings::Breakpoint::kEnabled) {
    return SettingValue(settings.enabled);
  } else if (key == ClientSettings::Breakpoint::kOneShot) {
    return SettingValue(settings.one_shot);
  } else if (key == ClientSettings::Breakpoint::kType) {
    return SettingValue(BreakpointSettings::TypeToString(settings.type));
  } else if (key == ClientSettings::Breakpoint::kSize) {
    return SettingValue(static_cast<int>(settings.byte_size));
  }
  FXL_NOTREACHED();
  return SettingValue();
}

Err Breakpoint::Settings::SetStorageValue(const std::string& key, SettingValue value) {
  BreakpointSettings settings = bp_->GetSettings();

  if (key == ClientSettings::Breakpoint::kLocation) {
    settings.locations = value.get_input_locations();
  } else if (key == ClientSettings::Breakpoint::kScope) {
    settings.scope = value.get_execution_scope();
  } else if (key == ClientSettings::Breakpoint::kStopMode) {
    std::optional<BreakpointSettings::StopMode> stop_mode =
        BreakpointSettings::StringToStopMode(value.get_string());
    FXL_DCHECK(stop_mode);  // Schema should have validated the input.
    settings.stop_mode = *stop_mode;
  } else if (key == ClientSettings::Breakpoint::kEnabled) {
    settings.enabled = value.get_bool();
  } else if (key == ClientSettings::Breakpoint::kOneShot) {
    settings.one_shot = value.get_bool();
  } else if (key == ClientSettings::Breakpoint::kType) {
    std::optional<BreakpointSettings::Type> type =
        BreakpointSettings::StringToType(value.get_string());
    FXL_DCHECK(type);  // Schema should have validated the input.
    settings.type = *type;
  } else if (key == ClientSettings::Breakpoint::kSize) {
    settings.byte_size = value.get_int();
  } else {
    FXL_NOTREACHED();
  }

  bp_->SetSettings(settings);
  return Err();
}

Breakpoint::Breakpoint(Session* session)
    : ClientObject(session), settings_(this), weak_factory_(this) {}
Breakpoint::~Breakpoint() {}

fxl::WeakPtr<Breakpoint> Breakpoint::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

// static
fxl::RefPtr<SettingSchema> Breakpoint::GetSchema() {
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
