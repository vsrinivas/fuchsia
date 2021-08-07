// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BUILDINFO_PROVIDER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BUILDINFO_PROVIDER_H_

#include <fuchsia/buildinfo/cpp/fidl_test_base.h>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.buildinfo.Provider.
class FakeBuildInfoProvider : public fuchsia::buildinfo::testing::Provider_TestBase {
 public:
  static constexpr char kProductConfig[] = "core";
  static constexpr char kBoardConfig[] = "x64";
  static constexpr char kVersion[] = "1980-01-01T00:00:00+00:00";
  static constexpr char kLatestCommitDate[] = "2021-01-01T00:00:00+00:00";

  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Constructs a FakeBuildInfoProvider using the provided configuration values.
  explicit FakeBuildInfoProvider(std::string product_config, std::string board_config,
                                 std::string version, std::string latest_commit_date)
      : product_config_(product_config),
        board_config_(board_config),
        version_(version),
        latest_commit_date_(latest_commit_date) {}

  // Constructs a FakeBuildInfoProvider using the default configuration values.
  FakeBuildInfoProvider()
      : FakeBuildInfoProvider(kProductConfig, kBoardConfig, kVersion, kLatestCommitDate) {}

  // Returns the current BuildInfo table.
  void GetBuildInfo(GetBuildInfoCallback callback) override {
    fuchsia::buildinfo::BuildInfo build_info;
    build_info.set_product_config(product_config_);
    build_info.set_board_config(board_config_);
    build_info.set_version(version_);
    build_info.set_latest_commit_date(latest_commit_date_);

    callback(std::move(build_info));
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::buildinfo::Provider> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::buildinfo::Provider> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Update the product configuration.
  FakeBuildInfoProvider& set_product_config(std::string product_config) {
    product_config_ = product_config;
    return *this;
  }

  // Update the board configuration.
  FakeBuildInfoProvider& set_board_config(std::string board_config) {
    board_config_ = board_config;
    return *this;
  }

  // Update the version.
  FakeBuildInfoProvider& set_version(std::string version) {
    version_ = version;
    return *this;
  }

  // Update the latest commit date.
  FakeBuildInfoProvider& set_latest_commit_date(std::string latest_commit_date) {
    latest_commit_date_ = latest_commit_date;
    return *this;
  }

 private:
  fidl::Binding<fuchsia::buildinfo::Provider> binding_{this};
  std::string product_config_;
  std::string board_config_;
  std::string version_;
  std::string latest_commit_date_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_BUILDINFO_PROVIDER_H_
