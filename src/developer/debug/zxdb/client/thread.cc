// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"

namespace zxdb {

// Schema Definition -------------------------------------------------------------------------------

const char* ClientSettings::Thread::kDebugStepping = "debug-stepping";
const char* ClientSettings::Thread::kDebugSteppingDescription =
    R"(  Enable very verbose debug logging for thread stepping.

  This is used by developers working on the debugger's internal thread
  controllers.)";

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  schema->AddBool(ClientSettings::Thread::kDebugStepping,
                  ClientSettings::Thread::kDebugSteppingDescription, false);

  return schema;
}

// Thread Implementation ---------------------------------------------------------------------------

Thread::Thread(Session* session)
    : ClientObject(session),
      // Implementations can set up fallbacks if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}

Thread::~Thread() = default;

fxl::WeakPtr<Thread> Thread::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

fxl::RefPtr<SettingSchema> Thread::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
