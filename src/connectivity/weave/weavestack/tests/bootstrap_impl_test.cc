// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/fidl/bootstrap_impl.h"

#include <fuchsia/weave/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

#include <gtest/gtest.h>
#include <src/lib/files/file.h>
#include <src/lib/fsl/vmo/strings.h>

namespace weavestack {
namespace {
using fuchsia::weave::Bootstrap_ImportWeaveConfig_Result;
}  // namespace

class TestableBootstrapImpl : public BootstrapImpl {
 public:
  TestableBootstrapImpl(sys::ComponentContext* context) : BootstrapImpl(context) {}
  void SetShouldServe(bool value) { should_serve_ = value; }

  void SetConfigPath(std::string config_path) { config_path_ = std::move(config_path); }
 private:
  std::string GetConfigPath() override { return config_path_; }
  bool ShouldServe() override { return should_serve_; }

  std::string config_path_;
  bool should_serve_;
};

class BootstrapImplTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();

    // Set up BootstrapImpl.
    ResetImpl(/*should_serve*/ true);

    // Connect to the interface under test.
    bootstrap_.set_error_handler([this](zx_status_t error) { last_error_ = error; });
    ReconnectBootstrapPtr();
    ASSERT_TRUE(bootstrap_.is_bound());
  }

 protected:
  fuchsia::weave::BootstrapPtr& bootstrap() { return bootstrap_; }
  TestableBootstrapImpl& bootstrap_impl() { return *bootstrap_impl_; }

  void ResetImpl(bool should_serve) {
    bootstrap_impl_ = std::make_unique<TestableBootstrapImpl>(provider_.context());
    bootstrap_impl_->SetShouldServe(should_serve);
    bootstrap_impl_->Init();
    RunLoopUntilIdle();
  }

  void ReconnectBootstrapPtr() {
    last_error_ = ZX_OK;
    provider_.ConnectToPublicService(bootstrap_.NewRequest());
    RunLoopUntilIdle();
  }

  zx_status_t last_error() { return last_error_; }

 private:
  sys::testing::ComponentContextProvider provider_;
  fuchsia::weave::BootstrapPtr bootstrap_;
  std::unique_ptr<TestableBootstrapImpl> bootstrap_impl_;
  zx_status_t last_error_;
};

// Test Cases ------------------------------------------------------------------

TEST_F(BootstrapImplTest, NoServe) {
  ResetImpl(/*should_serve*/ false);
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
}

TEST_F(BootstrapImplTest, ImportConfigHappy) {
  constexpr char kConfigPath[] = "/data/config-happy.json";
  bootstrap_impl().SetConfigPath(kConfigPath);

  constexpr char kContents[] = "{\"key\": \"value\"}";
  Bootstrap_ImportWeaveConfig_Result result;
  fuchsia::mem::Buffer buffer;

  ASSERT_TRUE(fsl::VmoFromString(kContents, &buffer));

  EXPECT_TRUE(bootstrap().is_bound());
  bool called = false;
  bootstrap()->ImportWeaveConfig(std::move(buffer), [&](Bootstrap_ImportWeaveConfig_Result r) {
    called = true;
    result = std::move(r);
  });
  RunLoopUntilIdle();

  // Confirm results.
  EXPECT_TRUE(called);
  EXPECT_TRUE(result.is_response());

  std::string contents;
  files::ReadFileToString(kConfigPath, &contents);

  EXPECT_EQ(kContents, contents);

  // Ensure binding is closed and FIDL is no longer serving.
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_OK, last_error());

  ReconnectBootstrapPtr();
  EXPECT_FALSE(bootstrap().is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, last_error());
}

TEST_F(BootstrapImplTest, ImportConfigFail) {
  constexpr char kConfigPath[] = "/data/config-fail.json";
  bootstrap_impl().SetConfigPath(kConfigPath);

  constexpr char kContents[] = "{\"key\": \"value\"}";
  Bootstrap_ImportWeaveConfig_Result result;
  fuchsia::mem::Buffer buffer;

  ASSERT_TRUE(fsl::VmoFromString(kContents, &buffer));

  // Restrict rights to cause read failure.
  zx::vmo temp;
  buffer.vmo.duplicate(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER, &temp);
  buffer.vmo = std::move(temp);

  EXPECT_TRUE(bootstrap().is_bound());
  bool called = false;
  bootstrap()->ImportWeaveConfig(std::move(buffer), [&](Bootstrap_ImportWeaveConfig_Result r) {
    called = true;
    result = std::move(r);
  });
  RunLoopUntilIdle();

  // Confirm results.
  EXPECT_TRUE(called);
  EXPECT_TRUE(result.is_err());
  EXPECT_EQ(ZX_ERR_IO, result.err());

  // Ensure binding is still intact and FIDL is still serving.
  EXPECT_TRUE(bootstrap().is_bound());

  ReconnectBootstrapPtr();
  EXPECT_TRUE(bootstrap().is_bound());
}

}  // namespace weavestack
