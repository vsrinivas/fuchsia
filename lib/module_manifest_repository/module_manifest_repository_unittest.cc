// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "peridot/lib/module_manifest_repository/module_manifest_repository.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace {

class ModuleManifestRepositoryTest : public testing::TestWithMessageLoop {
 public:
  void SetUp() override {
    testing::TestWithMessageLoop::SetUp();

    // Set up a temp dir for the repository.
    char temp_dir[] = "/tmp/module_manifest_repo_XXXXXX";
    FXL_CHECK(mkdtemp(temp_dir)) << strerror(errno);
    repo_dir_ = temp_dir;
  }
  void TearDown() override {
    // Clean up.
    for (const auto& path : manifests_written_) {
      remove(path.c_str());
    }

    rmdir(repo_dir_.c_str());

    testing::TestWithMessageLoop::TearDown();
  }

 protected:
  void ResetRepository() {
    auto task_runner = fsl::MessageLoop::GetCurrent()->task_runner();
    repo_.reset(new ModuleManifestRepository(repo_dir_));
    repo_->Watch(task_runner, [this](std::string id, ModuleManifestRepository::Entry entry) {
          entries_.push_back(entry);
          entry_ids_.push_back(std::move(id));
        }, [this](std::string id) {
          removed_ids_.push_back(std::move(id));
        });
  }

  void WriteManifestFile(const std::string& name,
                         const char* contents,
                         int len) {
    const auto path = repo_dir_ + '/' + name;
    FXL_CHECK(files::WriteFile(path, contents, len)) << path;
    manifests_written_.push_back(path);
  }

  void RemoveManifestFile(const std::string& name) {
    const auto path = repo_dir_ + '/' + name;
    unlink(path.c_str());
  }

  std::vector<std::string> manifests_written_;
  std::string repo_dir_;
  std::unique_ptr<ModuleManifestRepository> repo_;

  std::vector<ModuleManifestRepository::Entry> entries_;
  std::vector<std::string> entry_ids_;
  std::vector<std::string> removed_ids_;
};

const char* kManifest1 = R"END(
[
  {
    "binary": "m1_binary1",
    "local_name": "m1_local_name1",
    "verb": "com.google.fuchsia.navigate.v1",
    "noun_constraints": [
      {
        "name": "start",
        "types": [ "foo", "bar" ]
      },
      {
        "name": "destination",
        "types": [ "baz" ]
      }
    ]
  },
  {
    "binary": "m1_binary2",
    "local_name": "m1_local_name2",
    "verb": "com.google.fuchsia.pick.v1",
    "noun_constraints": [
      {
        "name": "thing",
        "types": [ "frob" ]
      }
    ]
  }
]
)END";

const char* kManifest2 = R"END(
[
  {
    "binary": "binary",
    "local_name": "local_name",
    "verb": "Annotate",
    "noun_constraints": [
      {
        "name": "thingy",
        "types": [ "chair" ]
      }
    ]
  }
]
)END";

TEST_F(ModuleManifestRepositoryTest, CreateFiles_And_CorrectEntries) {
  // Write a manifest file before creating the repo.
  WriteManifestFile("manifest1", kManifest1, strlen(kManifest1));

  ResetRepository();

  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 2; }));
  EXPECT_EQ("m1_binary1", entries_[0].binary);
  EXPECT_EQ("m1_local_name1", entries_[0].local_name);
  EXPECT_EQ("com.google.fuchsia.navigate.v1", entries_[0].verb);
  EXPECT_EQ(2lu, entries_[0].noun_constraints.size());
  EXPECT_EQ("start", entries_[0].noun_constraints[0].name);
  EXPECT_EQ(2lu, entries_[0].noun_constraints[0].types.size());
  EXPECT_EQ("foo", entries_[0].noun_constraints[0].types[0]);
  EXPECT_EQ("bar", entries_[0].noun_constraints[0].types[1]);
  EXPECT_EQ("destination", entries_[0].noun_constraints[1].name);
  EXPECT_EQ(1lu, entries_[0].noun_constraints[1].types.size());
  EXPECT_EQ("baz", entries_[0].noun_constraints[1].types[0]);

  EXPECT_EQ("m1_binary2", entries_[1].binary);
  EXPECT_EQ("m1_local_name2", entries_[1].local_name);
  EXPECT_EQ("com.google.fuchsia.pick.v1", entries_[1].verb);
  EXPECT_EQ(1lu, entries_[1].noun_constraints.size());
  EXPECT_EQ("thing", entries_[1].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries_[1].noun_constraints[0].types.size());
  EXPECT_EQ("frob", entries_[1].noun_constraints[0].types[0]);

  // Add a new file, expect to see the results.
  WriteManifestFile("manifest2", kManifest2, strlen(kManifest2));
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 3; }));

  EXPECT_EQ("binary", entries_[2].binary);
  EXPECT_EQ("local_name", entries_[2].local_name);
  EXPECT_EQ("Annotate", entries_[2].verb);
  EXPECT_EQ(1lu, entries_[2].noun_constraints.size());
  EXPECT_EQ("thingy", entries_[2].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries_[2].noun_constraints[0].types.size());
  EXPECT_EQ("chair", entries_[2].noun_constraints[0].types[0]);
}

TEST_F(ModuleManifestRepositoryTest, RemovedFiles) {
  // Write a manifest file before creating the repo.
  WriteManifestFile("manifest1", kManifest1, strlen(kManifest1));

  ResetRepository();
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 2; }));

  RemoveManifestFile("manifest1");
  ASSERT_TRUE(RunLoopUntil([this]() { return removed_ids_.size() == 2; }));
  EXPECT_EQ(removed_ids_[0], entry_ids_[0]);
  EXPECT_EQ(removed_ids_[1], entry_ids_[1]);
}

TEST_F(ModuleManifestRepositoryTest, RepoDirIsCreatedAutomatically) {
  repo_dir_ = "/tmp/foo";
  // TODO(thatguy): Once making ModuleManifestRepository easier to test against
  // (ie, have a guaranteed initialized state we can synchronize on), do that
  // here.
  ResetRepository();
  WriteManifestFile("manifest2", kManifest2, strlen(kManifest2));
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 1; }));
}

TEST_F(ModuleManifestRepositoryTest, IgnoreIncomingFiles) {
  ResetRepository();
  WriteManifestFile("foo.incoming", kManifest1, strlen(kManifest1));
  WriteManifestFile("foo", kManifest2, strlen(kManifest2));

  // The first manifest files contains two entries, but we should ignore them.
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 1; }));
}

}  // namespace
}  // namespace modular
