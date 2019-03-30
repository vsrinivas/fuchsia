// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job_context.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"

namespace zxdb {

// Schema Definition -----------------------------------------------------------

const char* ClientSettings::Job::kFilters = "filters";
static const char* kFiltersDescription =
    R"(  List of filters to be applied to a job. Filters are used against each new
  process being spawned under the attached job. If there is match, zxdb will
  automatically attach to the process.)";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>(SettingSchema::Level::kJob);
  schema->AddList(ClientSettings::Job::kFilters, kFiltersDescription, {});
  return schema;
}

}  // namespace

// JobContext Implemention -----------------------------------------------------

JobContext::JobContext(Session* session)
    : ClientObject(session),
      // Implementations can set up fallbacks if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}
JobContext::~JobContext() = default;

fxl::WeakPtr<JobContext> JobContext::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

fxl::RefPtr<SettingSchema> JobContext::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
