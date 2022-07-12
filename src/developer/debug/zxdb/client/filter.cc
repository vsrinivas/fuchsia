// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include <string_view>

#include "lib/syslog/cpp/macros.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"

namespace zxdb {

const char* ClientSettings::Filter::kType = "type";
const char* ClientSettings::Filter::kTypeDescription =
    R"(  The type of the filter. Could be "process name substr", "process name",
  "component name", "component url" or, "component moniker".)";

const char* ClientSettings::Filter::kPattern = "pattern";
const char* ClientSettings::Filter::kPatternDescription =
    R"(  The pattern used for matching. See "help attach" for more help.)";

const char* ClientSettings::Filter::kJob = "job";
const char* ClientSettings::Filter::kJobDescription =
    R"(  The scope of the filter. Only valid when the type is "process name substr" or
  "process name".)";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  schema->AddString(ClientSettings::Filter::kType, ClientSettings::Filter::kTypeDescription,
                    debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kUnset),
                    {debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kProcessNameSubstr),
                     debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kProcessName),
                     debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kComponentName),
                     debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kComponentUrl),
                     debug_ipc::Filter::TypeToString(debug_ipc::Filter::Type::kComponentMoniker)});
  schema->AddString(ClientSettings::Filter::kPattern, ClientSettings::Filter::kPatternDescription);
  schema->AddInt(ClientSettings::Filter::kJob, ClientSettings::Filter::kJobDescription);
  return schema;
}

debug_ipc::Filter::Type StringToType(std::string_view string) {
  for (int i = 0; i < static_cast<int>(debug_ipc::Filter::Type::kLast); i++) {
    auto type = static_cast<debug_ipc::Filter::Type>(i);
    if (string == debug_ipc::Filter::TypeToString(type))
      return type;
  }
  // Input should have been validated.
  FX_NOTREACHED();
  return debug_ipc::Filter::Type::kUnset;
}

}  // namespace

Filter::Settings::Settings(Filter* filter) : SettingStore(Filter::GetSchema()), filter_(filter) {}

SettingValue Filter::Settings::GetStorageValue(const std::string& key) const {
  if (key == ClientSettings::Filter::kType)
    return SettingValue(debug_ipc::Filter::TypeToString(filter_->filter_.type));
  if (key == ClientSettings::Filter::kPattern)
    return SettingValue(filter_->filter_.pattern);
  if (key == ClientSettings::Filter::kJob)
    return SettingValue(static_cast<int64_t>(filter_->filter_.job_koid));
  return SettingValue();
}

Err Filter::Settings::SetStorageValue(const std::string& key, SettingValue value) {
  // Schema should have been validated before here.
  if (key == ClientSettings::Filter::kType) {
    filter_->SetType(StringToType(value.get_string()));
  } else if (key == ClientSettings::Filter::kPattern) {
    filter_->SetPattern(value.get_string());
  } else if (key == ClientSettings::Filter::kJob) {
    if (filter_->filter_.type != debug_ipc::Filter::Type::kProcessNameSubstr &&
        filter_->filter_.type != debug_ipc::Filter::Type::kProcessName) {
      return Err("This filter type cannot be associated with a job.");
    }
    filter_->SetJobKoid(value.get_int());
  }
  return Err();
}

Filter::Filter(Session* session) : ClientObject(session), settings_(this) {}

void Filter::SetType(debug_ipc::Filter::Type type) {
  filter_.type = type;
  Sync();
}

void Filter::SetPattern(const std::string& pattern) {
  filter_.pattern = pattern;
  Sync();
}

void Filter::SetJobKoid(zx_koid_t job_koid) {
  filter_.job_koid = job_koid;
  Sync();
}

void Filter::Sync() { session()->system().SyncFilters(); }

// static
fxl::RefPtr<SettingSchema> Filter::GetSchema() {
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
