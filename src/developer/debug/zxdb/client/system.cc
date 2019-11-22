// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/system.h"

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/expr/vector_register_format.h"

namespace zxdb {

// Schema definition -------------------------------------------------------------------------------

const char* ClientSettings::System::kDebugMode = "debug-mode";
static const char* kDebugModeDescription =
    R"(  Output debug information about zxdb.
  In general should only be useful for people developing zxdb.)";

const char* ClientSettings::System::kSymbolPaths = "symbol-paths";
static const char* kSymbolPathsDescription =
    R"(  List of mapping databases, ELF files or directories for symbol lookup.
  When a directory path is passed, the directory will be enumerated
  non-recursively to index all ELF files within, unless it contains a .build-id
  subfolder, in which case that is presumed to contain an index of all ELF
  files within. When a .txt file is passed, it will be treated as a mapping
  database from build ID to file path. Otherwise, the path will be loaded as an
  ELF file.)";

const char* ClientSettings::System::kSymbolRepoPaths = "symbol-repo-paths";
static const char* kSymbolRepoPathsDescription =
    R"(  List of directories for symbol lookup. Each directory is assumed to
  contain a ".build-id"-style index of symbol files, but does not need to be
  named .build-id.)";

const char* ClientSettings::System::kPauseOnLaunch = "pause-on-launch";
static const char* kPauseOnLaunchDescription =
    R"(  Whether a process launched through zxdb should be stopped on startup.
  This will also affect components launched through zxdb.)";

const char* ClientSettings::System::kPauseOnAttach = "pause-on-attach";
static const char* kPauseOnAttachDescription =
    R"(  Whether the process should be paused when zxdb attached to it.
  This will also affect when zxdb attached a process through a filter.)";

const char* ClientSettings::System::kShowFilePaths = "show-file-paths";
static const char* kShowFilePathsDescription =
    R"(  Displays full path information when file names are displayed. Otherwise
  file names will be shortened to the shortest unique name in the current
  process.)";

const char* ClientSettings::System::kShowStdout = "show-stdout";
static const char* kShowStdoutDescription =
    R"(  Whether newly debugged process (either launched or attached) should
  output it's stdout/stderr to zxdb. This setting is global but can be overridden
  by each individual process.)";

const char* ClientSettings::System::kQuitAgentOnExit = "quit-agent-on-exit";
static const char* kQuitAgentOnExitDescription =
    R"(  Whether the client will shutdown the connected agent upon exiting.")";

const char* ClientSettings::System::kSymbolServers = "symbol-servers";
static const char* kSymbolServersDescription = R"(  List of symbol server URLs.)";

const char* ClientSettings::System::kSymbolCache = "symbol-cache";
static const char* kSymbolCacheDescription =
    R"(  Path to a writable directory for symbol data. A subdirectory named
  .build-id will be created under the given path, and downloaded symbols will
  be stored there.)";

namespace {

fxl::RefPtr<SettingSchema> CreateSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddBool(ClientSettings::System::kDebugMode, kDebugModeDescription, false);
  schema->AddList(ClientSettings::System::kSymbolPaths, kSymbolPathsDescription, {});
  schema->AddList(ClientSettings::System::kSymbolRepoPaths, kSymbolRepoPathsDescription, {});
  schema->AddBool(ClientSettings::System::kPauseOnLaunch, kPauseOnLaunchDescription, false);
  schema->AddBool(ClientSettings::System::kPauseOnAttach, kPauseOnAttachDescription, false);
  schema->AddBool(ClientSettings::System::kQuitAgentOnExit, kQuitAgentOnExitDescription, true);
  schema->AddBool(ClientSettings::System::kShowFilePaths, kShowFilePathsDescription, false);
  schema->AddBool(ClientSettings::System::kShowStdout, kShowStdoutDescription, true);
  schema->AddList(ClientSettings::System::kSymbolServers, kSymbolServersDescription, {});
  schema->AddString(ClientSettings::System::kSymbolCache, kSymbolCacheDescription, "");

  schema->AddList(ClientSettings::Target::kBuildDirs, ClientSettings::Target::kBuildDirsDescription,
                  {});

  schema->AddBool(ClientSettings::Thread::kDebugStepping,
                  ClientSettings::Thread::kDebugSteppingDescription, false);

  schema->AddString(
      ClientSettings::Target::kVectorFormat, ClientSettings::Target::kVectorFormatDescription,
      kVectorRegisterFormatStr_Double, ClientSettings::Target::GetVectorFormatOptions());

  return schema;
}

}  // namespace

// System Implementation ---------------------------------------------------------------------------

System::System(Session* session)
    : ClientObject(session), settings_(GetSchema(), nullptr), weak_factory_(this) {}

System::~System() = default;

fxl::WeakPtr<System> System::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void System::AddObserver(SystemObserver* observer) { observers_.AddObserver(observer); }

void System::RemoveObserver(SystemObserver* observer) { observers_.RemoveObserver(observer); }

fxl::RefPtr<SettingSchema> System::GetSchema() {
  // Will only run initialization once.
  InitializeSchemas();
  static fxl::RefPtr<SettingSchema> schema = CreateSchema();
  return schema;
}

}  // namespace zxdb
