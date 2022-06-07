// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_info.h"

#include <fuchsia/buildinfo/cpp/fidl.h>
#include <fuchsia/buildinfo/test/cpp/fidl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {
using component_testing::RealmBuilder;
using component_testing::RealmRoot;

using fuchsia::buildinfo::BuildInfo;
using fuchsia::buildinfo::Provider;
using fuchsia::buildinfo::test::BuildInfoTestController;

using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Route;
}  // namespace

class FakeBuildInfoTestFixture : public gtest::RealLoopFixture {
 public:
  static constexpr char fake_provider_url[] =
      "fuchsia-pkg://fuchsia.com/fake_build_info_test#meta/fake_build_info.cm";
  static constexpr char fake_provider_name[] = "fake_provider";

  static constexpr auto kProductFileName = "workstation";
  static constexpr auto kBoardFileName = "x64";
  static constexpr auto kVersionFileName = "2022-03-28T15:42:20+00:00";
  static constexpr auto kLastCommitDateFileName = "2022-03-28T15:42:20+00:00";

  FakeBuildInfoTestFixture()
      : realm_builder_(std::make_unique<RealmBuilder>(RealmBuilder::Create())) {}

  void SetUp() override {
    SetUpRealm(realm_builder_.get());

    realm_ = std::make_unique<RealmRoot>(realm_builder_->Build(dispatcher()));
  }

 protected:
  void SetUpRealm(RealmBuilder* builder) {
    realm_builder_->AddChild(fake_provider_name, fake_provider_url);

    realm_builder_->AddRoute(
        Route{.capabilities = {Protocol{Provider::Name_}, Protocol{BuildInfoTestController::Name_}},
              .source = ChildRef{fake_provider_name},
              .targets = {ParentRef()}});
  }

  RealmRoot* realm() { return realm_.get(); }

 private:
  std::unique_ptr<RealmRoot> realm_;
  std::unique_ptr<RealmBuilder> realm_builder_;
};

TEST_F(FakeBuildInfoTestFixture, SetBuildInfo) {
  auto provider = realm()->ConnectSync<Provider>();
  auto test_controller = realm()->ConnectSync<BuildInfoTestController>();

  BuildInfo result;
  provider->GetBuildInfo(&result);

  EXPECT_TRUE(result.has_product_config());
  EXPECT_EQ(result.product_config(), FakeProviderImpl::kProductFileNameDefault);
  EXPECT_TRUE(result.has_board_config());
  EXPECT_EQ(result.board_config(), FakeProviderImpl::kBoardFileNameDefault);
  EXPECT_TRUE(result.has_version());
  EXPECT_EQ(result.version(), FakeProviderImpl::kVersionFileNameDefault);
  EXPECT_TRUE(result.has_latest_commit_date());
  EXPECT_EQ(result.latest_commit_date(), FakeProviderImpl::kLastCommitDateFileNameDefault);

  auto build_info = BuildInfo();
  build_info.set_board_config(FakeBuildInfoTestFixture::kBoardFileName);
  build_info.set_product_config(FakeBuildInfoTestFixture::kProductFileName);
  build_info.set_version(FakeBuildInfoTestFixture::kVersionFileName);
  build_info.set_latest_commit_date(FakeBuildInfoTestFixture::kLastCommitDateFileName);

  test_controller->SetBuildInfo(std::move(build_info));
  provider->GetBuildInfo(&result);

  EXPECT_TRUE(result.has_product_config());
  EXPECT_EQ(result.product_config(), FakeBuildInfoTestFixture::kProductFileName);
  EXPECT_TRUE(result.has_board_config());
  EXPECT_EQ(result.board_config(), FakeBuildInfoTestFixture::kBoardFileName);
  EXPECT_TRUE(result.has_version());
  EXPECT_EQ(result.version(), FakeBuildInfoTestFixture::kVersionFileName);
  EXPECT_TRUE(result.has_latest_commit_date());
  EXPECT_EQ(result.latest_commit_date(), FakeBuildInfoTestFixture::kLastCommitDateFileName);
}
