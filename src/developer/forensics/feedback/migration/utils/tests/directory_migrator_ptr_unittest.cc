// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/directory_migrator_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fdio/fd.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/feedback/internal/cpp/fidl.h"
#include "src/developer/forensics/feedback/migration/utils/file_utils.h"
#include "src/developer/forensics/feedback/migration/utils/tests/directory_migrator_stubs.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback {
namespace {

using DirectoryMigratorForTest =
    DirectoryMigratorStub<fuchsia::feedback::internal::FeedbackDataDirectoryMigrator>;

class DirectoryMigratorPtrTest : public UnitTestFixture {
 public:
  DirectoryMigratorPtrTest() : executor_(dispatcher()), migrator_(dispatcher()) {}

 protected:
  void SetUpMigratorServer(
      std::optional<std::string> data_path, std::optional<std::string> cache_path,
      std::optional<DirectoryMigratorForTest::ErrorResponse> error_response = std::nullopt) {
    FX_CHECK(!migrator_server_);
    migrator_server_.emplace(std::move(data_path), std::move(cache_path), error_response);
    InjectServiceProvider(&migrator_server_.value());
  }

  FeedbackDataDirectoryMigrator::Directories GetDirectoriesOk() {
    if (!migrator_.IsBound()) {
      FX_CHECK(services()->Connect(migrator_.NewRequest()) == ZX_OK);
    }

    bool called{false};
    fbl::unique_fd data;
    fbl::unique_fd cache;

    executor_.schedule_task(migrator_.GetDirectories(zx::duration::infinite())
                                .and_then([&](FeedbackDataDirectoryMigrator::Directories& dirs) {
                                  called = true;
                                  std::tie(data, cache) = std::move(dirs);
                                })
                                .or_else([](const Error& error) { FX_CHECK(false); }));
    RunLoopUntilIdle();

    FX_CHECK(called);
    return std::make_pair(std::move(data), std::move(cache));
  }

  Error GetDirectoriesError(const zx::duration timeout) {
    if (!migrator_.IsBound()) {
      FX_CHECK(services()->Connect(migrator_.NewRequest()) == ZX_OK);
    }

    bool called{false};
    Error error{Error::kDefault};

    executor_.schedule_task(
        migrator_.GetDirectories(timeout)
            .and_then([](FeedbackDataDirectoryMigrator::Directories& dirs) { FX_CHECK(false); })
            .or_else([&](const Error& e) {
              called = true;
              error = e;
            }));
    RunLoopFor(timeout);

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

TEST_F(DirectoryMigratorPtrTest, ConnctionDropped) {
  files::ScopedTempDir data_dir;
  files::ScopedTempDir cache_dir;

  SetUpMigratorServer(data_dir.path(), cache_dir.path(),
                      DirectoryMigratorForTest::ErrorResponse::kDropConnection);
  EXPECT_EQ(GetDirectoriesError(zx::min(1)), Error::kConnectionError);
}

TEST_F(DirectoryMigratorPtrTest, NoServer) {
  EXPECT_EQ(GetDirectoriesError(zx::min(1)), Error::kConnectionError);
}

TEST_F(DirectoryMigratorPtrTest, Timeout) {
  files::ScopedTempDir data_dir;
  files::ScopedTempDir cache_dir;

  SetUpMigratorServer(data_dir.path(), cache_dir.path(),
                      DirectoryMigratorForTest::ErrorResponse::kHang);
  EXPECT_EQ(GetDirectoriesError(zx::min(1)), Error::kTimeout);
}

}  // namespace
}  // namespace feedback
}  // namespace forensics
