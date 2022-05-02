// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUILD_INFO_TESTING_BUILD_INFO_H_
#define SRC_DEVELOPER_BUILD_INFO_TESTING_BUILD_INFO_H_

#include <fuchsia/buildinfo/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>

// Stores the build information values set by the test controller.
struct fake_info {
  std::string product_config_;
  std::string board_config_;
  std::string version_;
  std::string latest_commit_date_;
};

// Sets fake system build information. Used for testing.
class BuildInfoTestControllerImpl : public fuchsia::buildinfo::test::BuildInfoTestController {
 public:
  explicit BuildInfoTestControllerImpl(std::shared_ptr<struct fake_info> info_ref)
      : info_ref_(std::move(info_ref)) {}

  // Set the value to be returned by GetBuildInfo() in the Provider.
  void SetBuildInfo(fuchsia::buildinfo::BuildInfo build_info,
                    SetBuildInfoCallback callback) override;

 private:
  std::shared_ptr<struct fake_info> info_ref_;
};

// Returns fake system build information. Used for testing.
class FakeProviderImpl : public fuchsia::buildinfo::Provider {
 public:
  static constexpr auto kProductFileNameDefault = "core";
  static constexpr auto kBoardFileNameDefault = "chromebook-x64";
  static constexpr auto kVersionFileNameDefault = "2019-03-28T09:00:20+00:00";
  static constexpr auto kLastCommitDateFileNameDefault = "2019-03-28T09:00:20+00:00";

  explicit FakeProviderImpl(std::shared_ptr<struct fake_info> info_ref)
      : info_ref_(std::move(info_ref)) {}

  // Returns a fake product, board, version, and timestamp information used at build time.
  void GetBuildInfo(GetBuildInfoCallback callback) override;

 private:
  std::shared_ptr<struct fake_info> info_ref_;
};
#endif  // SRC_DEVELOPER_BUILD_INFO_TESTING_BUILD_INFO_H_
