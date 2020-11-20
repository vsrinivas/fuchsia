// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_info.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/status.h>

namespace {
const char kFuchsiaBuildInfoDirectoryPath[] = "/config/build-info";

const char kProductFileName[] = "product";
const char kBoardFileName[] = "board";
const char kVersionFileName[] = "version";
const char kLastCommitDateFileName[] = "latest-commit-date";
const char kSnapshotFileName[] = "snapshot";
}  // namespace

class BuildInfoServiceInstance {
 public:
  explicit BuildInfoServiceInstance(std::unique_ptr<sys::ComponentContext> context) {
    context_ = std::move(context);
    binding_ = std::make_unique<fidl::Binding<fuchsia::buildinfo::Provider>>(&impl_);
    fidl::InterfaceRequestHandler<fuchsia::buildinfo::Provider> handler =
        [&](fidl::InterfaceRequest<fuchsia::buildinfo::Provider> request) {
          binding_->Bind(std::move(request));
        };
    context_->outgoing()->AddPublicService(std::move(handler));
  }

 private:
  ProviderImpl impl_;
  std::unique_ptr<fidl::Binding<fuchsia::buildinfo::Provider>> binding_;
  std::unique_ptr<sys::ComponentContext> context_;
};

class BuildInfoServiceTestFixture : public gtest::TestLoopFixture {
 public:
  BuildInfoServiceTestFixture() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread){};

  void SetUp() override {
    TestLoopFixture::SetUp();
    build_info_service_instance_.reset(new BuildInfoServiceInstance(provider_.TakeContext()));

    loop_.StartThread();

    // Create a channel.
    zx_handle_t endpoint0;
    zx_handle_t endpoint1;
    zx_status_t status = zx_channel_create(0, &endpoint0, &endpoint1);
    ZX_ASSERT_MSG(status == ZX_OK, "Cannot create channel: %s\n", zx_status_get_string(status));

    // Get the process's namespace.
    fdio_ns_t *ns;
    status = fdio_ns_get_installed(&ns);
    ZX_ASSERT_MSG(status == ZX_OK, "Cannot get namespace: %s\n", zx_status_get_string(status));

    // Create the /config/build-info path in the namespace.
    std::string build_info_directory_path(kFuchsiaBuildInfoDirectoryPath);
    status = fdio_ns_bind(ns, build_info_directory_path.c_str(), endpoint0);
    ZX_ASSERT_MSG(status == ZX_OK, "Cannot bind %s to namespace: %s\n",
                  build_info_directory_path.c_str(), zx_status_get_string(status));

    // Connect the build-info PseudoDir to the /config/build-info path.
    zx::channel channel(endpoint1);
    build_info_directory_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                                std::move(channel), loop_.dispatcher());
  }

  // Creates a PsuedoDir named |build_info_filename| in the PsuedoDir "/config/build-info" in
  // the component's namespace. The file contains |build_info_filename| followed optionally by
  // a trailing newline.
  void CreateBuildInfoFile(std::string build_info_filename, bool with_trailing_newline = true) {
    std::string file_contents(build_info_filename);

    // Some build info files contain a trailing newline which the build info service strips.
    // Optionally add a trailing newline to test the trailing whitespace stripping.
    if (with_trailing_newline) {
      file_contents.append("\n");
    }

    vfs::PseudoFile::ReadHandler versionFileReadFn = [file_contents](std::vector<uint8_t> *output,
                                                                     size_t max_file_size) {
      output->resize(file_contents.length());
      std::copy(file_contents.begin(), file_contents.end(), output->begin());
      return ZX_OK;
    };
    vfs::PseudoFile::WriteHandler versionFileWriteFn;

    // Create a PseudoFile.
    std::unique_ptr<vfs::PseudoFile> pseudo_file = std::make_unique<vfs::PseudoFile>(
        file_contents.length(), std::move(versionFileReadFn), std::move(versionFileWriteFn));

    // Add the file to the build-info PseudoDir.
    build_info_directory_.AddEntry(std::move(build_info_filename), std::move(pseudo_file));
  }

  void TearDown() override {
    TestLoopFixture::TearDown();
    build_info_service_instance_.reset();
    DestroyBuildInfoFile();
  }

 protected:
  fuchsia::buildinfo::ProviderPtr GetProxy() {
    fuchsia::buildinfo::ProviderPtr provider;
    provider_.ConnectToPublicService(provider.NewRequest());
    return provider;
  }

 private:
  void DestroyBuildInfoFile() {
    fdio_ns_t *ns;
    zx_status_t status = fdio_ns_get_installed(&ns);
    std::string build_info_directory_path(kFuchsiaBuildInfoDirectoryPath);
    status = fdio_ns_unbind(ns, build_info_directory_path.c_str());

    loop_.Quit();
    loop_.JoinThreads();
  }

  std::unique_ptr<BuildInfoServiceInstance> build_info_service_instance_;
  sys::testing::ComponentContextProvider provider_;
  vfs::PseudoDir build_info_directory_;
  async::Loop loop_;
};

TEST_F(BuildInfoServiceTestFixture, BuildInfo) {
  CreateBuildInfoFile(kProductFileName);
  CreateBuildInfoFile(kBoardFileName);
  CreateBuildInfoFile(kVersionFileName);
  CreateBuildInfoFile(kLastCommitDateFileName);

  fuchsia::buildinfo::ProviderPtr proxy = GetProxy();
  proxy->GetBuildInfo([&](const fuchsia::buildinfo::BuildInfo &response) {
    EXPECT_TRUE(response.has_product_config());
    EXPECT_EQ(response.product_config(), kProductFileName);
    EXPECT_TRUE(response.has_board_config());
    EXPECT_EQ(response.board_config(), kBoardFileName);
    EXPECT_TRUE(response.has_version());
    EXPECT_EQ(response.version(), kVersionFileName);
    EXPECT_TRUE(response.has_latest_commit_date());
    EXPECT_EQ(response.latest_commit_date(), kLastCommitDateFileName);
  });

  RunLoopUntilIdle();
}

TEST_F(BuildInfoServiceTestFixture, Snapshot) {
  CreateBuildInfoFile(kSnapshotFileName, false);

  fuchsia::buildinfo::ProviderPtr proxy = GetProxy();
  proxy->GetSnapshotInfo([&](zx::vmo response) {
    uint64_t size;
    response.get_size(&size);
    auto buffer = std::make_unique<char[]>(size);
    response.read(buffer.get(), 0, size);
    std::string response_string{buffer.get()};

    EXPECT_EQ(response_string, kSnapshotFileName);
  });

  RunLoopUntilIdle();
}
