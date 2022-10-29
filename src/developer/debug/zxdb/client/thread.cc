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

const char* ClientSettings::Thread::kDisplay = "display";
const char* ClientSettings::Thread::kDisplayDescription =
    R"(  Lists expressions and variables to print every time the debugger stops.

  An alternative to modifying this list is the "display" verb which appends
  an expression to the global list. It's an alias for:
    global set display += "<expression>")";

namespace {

static fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  schema->AddBool(ClientSettings::Thread::kDebugStepping,
                  ClientSettings::Thread::kDebugSteppingDescription, false);
  schema->AddList(ClientSettings::Thread::kDisplay, ClientSettings::Thread::kDisplayDescription);
  return schema;
}

}  // namespace

// Thread Implementation ---------------------------------------------------------------------------

Thread::Thread(Session* session)
    : ClientObject(session),
      // Implementations can set up fallbacks if needed.
      settings_(GetSchema(), nullptr),
      weak_factory_(this) {}

Thread::~Thread() = default;

fxl::WeakPtr<Thread> Thread::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

bool Thread::IsBlockedOnException() const {
  std::optional<debug_ipc::ThreadRecord::State> state_or = GetState();
  return state_or && *state_or == debug_ipc::ThreadRecord::State::kBlocked &&
         GetBlockedReason() == debug_ipc::ThreadRecord::BlockedReason::kException;
}

bool Thread::CurrentStopSupportsFrames() const {
  std::optional<debug_ipc::ThreadRecord::State> state_or = GetState();
  return IsBlockedOnException() ||
         (state_or && (*state_or == debug_ipc::ThreadRecord::State::kCoreDump ||
                       *state_or == debug_ipc::ThreadRecord::State::kSuspended));
}

fxl::RefPtr<SettingSchema> Thread::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
