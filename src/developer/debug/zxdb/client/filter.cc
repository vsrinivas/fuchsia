// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"

namespace zxdb {

const char* ClientSettings::Filter::kPattern = "pattern";
const char* ClientSettings::Filter::kPatternDescription =
    R"( The filter to apply. Processes launched in the associated attached job will
  have this substring matched against their process name.)";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  schema->AddString(ClientSettings::Filter::kPattern, ClientSettings::Filter::kPatternDescription);
  return schema;
}

}  // namespace

const char Filter::kAllProcessesPattern[] = "*";

Filter::Settings::Settings(Filter* filter) : SettingStore(Filter::GetSchema()), filter_(filter) {}

SettingValue Filter::Settings::GetStorageValue(const std::string& key) const {
  if (key == ClientSettings::Filter::kPattern)
    return SettingValue(filter_->pattern_);
  return SettingValue();
}

Err Filter::Settings::SetStorageValue(const std::string& key, SettingValue value) {
  // Schema should have been validated before here.
  FXL_DCHECK(key == ClientSettings::Filter::kPattern);
  filter_->pattern_ = value.get_string();
  return Err();
}

Filter::Filter(Session* session) : ClientObject(session), settings_(this) {
  for (auto& observer : this->session()->filter_observers()) {
    observer.DidCreateFilter(this);
  }
}

Filter::~Filter() {
  for (auto& observer : session()->filter_observers()) {
    observer.WillDestroyFilter(this);
  }
}

void Filter::SetPattern(const std::string& pattern) {
  pattern_ = pattern;

  if (is_valid()) {
    for (auto& observer : session()->filter_observers()) {
      observer.DidChangeFilter(this, job());
    }
  }
}

void Filter::SetJob(JobContext* job) {
  std::optional<JobContext*> previous(this->job());

  if (!is_valid()) {
    previous = std::nullopt;
  }

  job_ = job ? std::optional(job->GetWeakPtr()) : std::nullopt;
  for (auto& observer : session()->filter_observers()) {
    observer.DidChangeFilter(this, previous);
  }
}

// static
fxl::RefPtr<SettingSchema> Filter::GetSchema() {
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
