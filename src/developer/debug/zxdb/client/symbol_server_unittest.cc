// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/symbol_server.h"

#include <stdlib.h>

#include <filesystem>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/cloud_storage_symbol_server.h"
#include "src/developer/debug/zxdb/client/mock_process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

class SymbolServerTest : public TestWithLoop {
 public:
  SymbolServerTest() : session_() {
    static auto fake_home =
        std::filesystem::path(GetSelfPath()).parent_path() / "test_data" / "zxdb" / "fake_home";
    setenv("HOME", fake_home.string().c_str(), true);
    unsetenv("XDG_CACHE_HOME");

    auto server = std::make_unique<MockCloudStorageSymbolServer>(&session_, "gs://fake-bucket");
    server_ = server.get();
    session_.system_impl().InjectSymbolServerForTesting(std::move(server));
  }

  virtual ~SymbolServerTest() = default;

  Session* session() { return &session_; }
  MockCloudStorageSymbolServer* server() { return server_; }

  void QuietlyFinishInit() {
    server()->on_do_authenticate = [](const std::map<std::string, std::string>& data,
                                      fit::callback<void(const Err&)>) {};
    server()->InitForTest();
    server()->ForceReady();
  }

  Process* CreateTestProcess() {
    TargetImpl* target = session()->system_impl().GetTargetImpls()[0];
    target->CreateProcessForTesting(1234, "foo");
    return target->GetProcess();
  }

 private:
  Session session_;
  MockCloudStorageSymbolServer* server_;
};

TEST_F(SymbolServerTest, LoadAuth) {
  std::map<std::string, std::string> got;

  server()->on_do_authenticate = [&got](const std::map<std::string, std::string>& data,
                                        fit::callback<void(const Err&)>) { got = data; };

  server()->InitForTest();

  EXPECT_EQ(4u, got.size());
  EXPECT_NE(got.end(), got.find("client_id"));
  EXPECT_NE(got.end(), got.find("client_secret"));
  EXPECT_EQ("refresh_token", got["grant_type"]);
  EXPECT_EQ("ThisIsATestFile\n", got["refresh_token"]);
}

TEST_F(SymbolServerTest, DownloadTypes) {
  QuietlyFinishInit();
  Process* process = CreateTestProcess();

  debug_ipc::Module module;
  module.name = "a_module";
  module.base = 0;
  module.build_id = "1234";

  bool saw_weird_module = false;
  bool saw_binary_request = false;
  bool saw_symbol_request = false;

  server()->on_check_fetch = [this, &saw_weird_module, &saw_binary_request, &saw_symbol_request](
                                 const std::string& build_id, DebugSymbolFileType file_type,
                                 SymbolServer::CheckFetchCallback cb) {
    saw_weird_module = build_id != "1234";
    saw_binary_request = saw_binary_request || file_type == DebugSymbolFileType::kBinary;
    saw_symbol_request = saw_symbol_request || file_type == DebugSymbolFileType::kDebugInfo;

    if (saw_weird_module || (saw_binary_request && saw_symbol_request)) {
      loop().QuitNow();
    }
  };

  process->GetSymbols()->SetModules({module});

  EXPECT_FALSE(saw_weird_module);
  EXPECT_FALSE(saw_binary_request);
  EXPECT_TRUE(saw_symbol_request);
}

}  // namespace zxdb
