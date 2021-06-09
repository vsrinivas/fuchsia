// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/directory_migrator_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fdio/fd.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/migration/utils/file_utils.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback {
namespace {

class DirectoryMigratorForTest : public fuchsia::feedback::internal::FeedbackDataDirectoryMigrator {
 public:
  DirectoryMigratorForTest(std::optional<std::string> data_path,
                           std::optional<std::string> cache_path)
      : data_path_(std::move(data_path)), cache_path_(std::move(cache_path)) {}

  void GetDirectories(GetDirectoriesCallback callback) override {
    callback(PathToInterfaceHandle(data_path_), PathToInterfaceHandle(cache_path_));
  }

  fidl::InterfaceRequestHandler<fuchsia::feedback::internal::FeedbackDataDirectoryMigrator>
  GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::internal::FeedbackDataDirectoryMigrator>
                      request) { bindings_.AddBinding(this, std::move(request)); };
  }

 private:
  ::fidl::InterfaceHandle<fuchsia::io::Directory> PathToInterfaceHandle(
      const std::optional<std::string>& path) {
    ::fidl::InterfaceHandle<fuchsia::io::Directory> handle;
    if (!path) {
      return handle;
    }

    return IntoInterfaceHandle(fbl::unique_fd(open(path->c_str(), O_DIRECTORY | O_RDWR, 0777)));
  }

  std::optional<std::string> data_path_;
  std::optional<std::string> cache_path_;

  fidl::BindingSet<fuchsia::feedback::internal::FeedbackDataDirectoryMigrator> bindings_;
};

class DirectoryMigratorPtrTest : public UnitTestFixture {
 public:
  DirectoryMigratorPtrTest() : executor_(dispatcher()) {}

 protected:
  void SetUpMigratorServer(std::optional<std::string> data_path,
                           std::optional<std::string> cache_path) {
    FX_CHECK(!migrator_server_);
    migrator_server_.emplace(std::move(data_path), std::move(cache_path));
    InjectServiceProvider(&migrator_server_.value());
  }

  FeedbackDataDirectoryMigrator::Directories GetDirectoriesOk() {
    if (!migrator_.IsBound()) {
      FX_CHECK(services()->Connect(migrator_.NewRequest()) == ZX_OK);
    }

    bool called{false};
    fbl::unique_fd data;
    fbl::unique_fd cache;

    executor_.schedule_task(migrator_.GetDirectories()
                                .and_then([&](FeedbackDataDirectoryMigrator::Directories& dirs) {
                                  called = true;
                                  std::tie(data, cache) = std::move(dirs);
                                })
                                .or_else([](const Error& error) { FX_CHECK(false); }));
    RunLoopUntilIdle();

    FX_CHECK(called);
    return std::make_pair(std::move(data), std::move(cache));
  }

  Error GetDirectoriesError() {
    if (!migrator_.IsBound()) {
      FX_CHECK(services()->Connect(migrator_.NewRequest()) == ZX_OK);
    }

    bool called{false};
    Error error{Error::kDefault};

    executor_.schedule_task(
        migrator_.GetDirectories()
            .and_then([](FeedbackDataDirectoryMigrator::Directories& dirs) { FX_CHECK(false); })
            .or_else([&](const Error& e) {
              called = true;
              error = e;
            }));
    RunLoopUntilIdle();

    FX_CHECK(called);
    return error;
  }

 private:
  async::Executor executor_;
  FeedbackDataDirectoryMigrator migrator_;
  std::optional<DirectoryMigratorForTest> migrator_server_;
};

TEST_F(DirectoryMigratorPtrTest, ValidDirectories) {
  files::ScopedTempDir data_dir;
  files::ScopedTempDir cache_dir;

  SetUpMigratorServer(data_dir.path(), cache_dir.path());
  auto [data_fd, cache_fd] = GetDirectoriesOk();

  EXPECT_TRUE(data_fd.is_valid());
  EXPECT_TRUE(cache_fd.is_valid());
}

TEST_F(DirectoryMigratorPtrTest, MissingDirectory) {
  files::ScopedTempDir data_dir;

  SetUpMigratorServer(data_dir.path(), std::nullopt);
  auto [data_fd, cache_fd] = GetDirectoriesOk();

  EXPECT_TRUE(data_fd.is_valid());
  EXPECT_FALSE(cache_fd.is_valid());
}

TEST_F(DirectoryMigratorPtrTest, BadDirectory) {
  files::ScopedTempDir data_dir;

  SetUpMigratorServer(data_dir.path(), "/bad/path");
  auto [data_fd, cache_fd] = GetDirectoriesOk();

  EXPECT_TRUE(data_fd.is_valid());
  EXPECT_FALSE(cache_fd.is_valid());
}

TEST_F(DirectoryMigratorPtrTest, NoServer) {
  EXPECT_EQ(GetDirectoriesError(), Error::kConnectionError);
}

}  // namespace
}  // namespace feedback
}  // namespace forensics
