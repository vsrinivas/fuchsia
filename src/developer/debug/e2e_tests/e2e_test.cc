// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/e2e_tests/e2e_test.h"

#include <filesystem>

#include "src/developer/debug/e2e_tests/main_e2e_test.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/host_util.h"

namespace zxdb {

E2eTest::E2eTest() {
  session_ = std::make_unique<Session>();

  // Setup symbols.
  std::filesystem::path symbol_dir =
      std::filesystem::path(GetSelfPath()).parent_path() / ZXDB_E2E_TESTS_SYMBOL_DIR;
  session_->system().settings().SetList(ClientSettings::System::kBuildIdDirs,
                                        {symbol_dir.string()});

  session_->breakpoint_observers().AddObserver(this);
  session_->component_observers().AddObserver(this);
  session_->process_observers().AddObserver(this);
  session_->thread_observers().AddObserver(this);

  // Use mock console so we don't have to deal with plaintext output in CI environment which won't
  // handle control characters from the line input library or UTF-8 characters. Any output will be
  // from reporting errors directly from the test.
  console_ = std::make_unique<MockConsole>(session_.get());
  console_->Init();

  FX_CHECK(bridge) << "debug_agent bridge failed to initialize.";
  socket_path_ = bridge->GetDebugAgentSocketPath();

  Err e = ConnectToDebugAgent();
  FX_CHECK(e.ok()) << e.msg();
  FX_CHECK(session().IsConnected()) << "Not connected to DebugAgent.";
}

E2eTest::~E2eTest() {
  // Needs to be destructed before |session_|.
  console_->Quit();
  console_.reset();

  Err e = session_->Disconnect();
  FX_CHECK(e.ok()) << e.msg();

  session_->breakpoint_observers().RemoveObserver(this);
  session_->component_observers().RemoveObserver(this);
  session_->process_observers().RemoveObserver(this);
  session_->thread_observers().RemoveObserver(this);

  session_.reset();
}

Err E2eTest::ConnectToDebugAgent() {
  SessionConnectionInfo info;
  info.type = SessionConnectionType::kUnix;
  info.host = socket_path_;

  Err err;

  session_->Connect(info, [&err, this](const Err& e) {
    err = e;
    loop().QuitNow();
  });

  loop().Run();

  return err;
}

}  // namespace zxdb
