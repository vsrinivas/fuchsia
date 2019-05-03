// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/symbol_server.h"

#include <stdlib.h>

#include <filesystem>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/cloud_storage_symbol_server.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/host_util.h"

namespace zxdb {

class SymbolServerTest : public testing::Test {
 public:
  SymbolServerTest() : session_(), server_(&session_, "gs://fake-bucket") {
    static auto fake_home = std::filesystem::path(GetSelfPath()).parent_path() /
                            "test_data" / "zxdb" / "fake_home";
    setenv("HOME", fake_home.string().c_str(), true);
    unsetenv("XDG_CACHE_HOME");
  }

  virtual ~SymbolServerTest() = default;

  Session* session() { return &session_; }
  MockCloudStorageSymbolServer* server() { return &server_; }

 private:
  Session session_;
  MockCloudStorageSymbolServer server_;
};

TEST_F(SymbolServerTest, LoadAuth) {
  std::map<std::string, std::string> got;

  server()->on_do_authenticate =
      [&got](const std::map<std::string, std::string>& data,
             std::function<void(const Err&)>) { got = data; };

  server()->InitForTest();

  EXPECT_EQ(4u, got.size());
  EXPECT_NE(got.end(), got.find("client_id"));
  EXPECT_NE(got.end(), got.find("client_secret"));
  EXPECT_EQ("refresh_token", got["grant_type"]);
  EXPECT_EQ("ThisIsATestFile\n", got["refresh_token"]);
}

}  // namespace zxdb
