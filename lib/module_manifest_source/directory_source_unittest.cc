// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "peridot/lib/module_manifest_source/directory_source.h"
#include "peridot/lib/module_manifest_source/module_manifest_source.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace {

class DirectoryModuleManifestSourceTest : public testing::TestWithMessageLoop {
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
  void Reset(bool create_dir = false) {
    auto task_runner = fsl::MessageLoop::GetCurrent()->task_runner();
    idle_ = false;
    repo_.reset(new DirectoryModuleManifestSource(repo_dir_, create_dir));
    repo_->Watch(
        task_runner, [this]() { idle_ = true; },
        [this](std::string id, ModuleManifestSource::Entry entry) {
          entries_.push_back(entry);
          entry_ids_.push_back(std::move(id));
        },
        [this](std::string id) { removed_ids_.push_back(std::move(id)); });
  }

  void WriteManifestFile(const std::string& name, const char* contents) {
    const auto path = repo_dir_ + '/' + name;
    FXL_CHECK(files::WriteFile(path, contents, strlen(contents))) << path;
    manifests_written_.push_back(path);
  }

  void RemoveManifestFile(const std::string& name) {
    const auto path = repo_dir_ + '/' + name;
    unlink(path.c_str());
  }

  bool idle_;

  std::vector<std::string> manifests_written_;
  std::string repo_dir_;
  std::unique_ptr<DirectoryModuleManifestSource> repo_;

  std::vector<ModuleManifestSource::Entry> entries_;
  std::vector<std::string> entry_ids_;
  std::vector<std::string> removed_ids_;
};

const char* kManifest1 = R"END(
{
  "binary": "binary1",
  "local_name": "local_name1",
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
}
)END";
const char* kManifest2 = R"END(
{
  "binary": "binary2",
  "local_name": "local_name2",
  "verb": "com.google.fuchsia.pick.v1",
  "noun_constraints": [
    {
      "name": "thing",
      "types": [ "frob" ]
    }
  ]
}
)END";
const char* kManifest3 = R"END(
{
  "binary": "binary3",
  "local_name": "local_name3",
  "verb": "com.google.fuchsia.annotate.v1",
  "noun_constraints": [
    {
      "name": "thingy",
      "types": [ "chair" ]
    }
  ]
}
)END";

TEST_F(DirectoryModuleManifestSourceTest, CreateFiles_And_CorrectEntries) {
  // Write a manifest file before creating the repo.
  WriteManifestFile("manifest1", kManifest1);
  WriteManifestFile("manifest2", kManifest2);

  Reset();

  ASSERT_TRUE(RunLoopUntil([this]() { return idle_; }));
  ASSERT_EQ(2lu, entries_.size());
  EXPECT_EQ("binary1", entries_[0].binary);
  EXPECT_EQ("local_name1", entries_[0].local_name);
  EXPECT_EQ("com.google.fuchsia.navigate.v1", entries_[0].verb);
  EXPECT_EQ(2lu, entries_[0].noun_constraints.size());
  EXPECT_EQ("start", entries_[0].noun_constraints[0].name);
  EXPECT_EQ(2lu, entries_[0].noun_constraints[0].types.size());
  EXPECT_EQ("foo", entries_[0].noun_constraints[0].types[0]);
  EXPECT_EQ("bar", entries_[0].noun_constraints[0].types[1]);
  EXPECT_EQ("destination", entries_[0].noun_constraints[1].name);
  EXPECT_EQ(1lu, entries_[0].noun_constraints[1].types.size());
  EXPECT_EQ("baz", entries_[0].noun_constraints[1].types[0]);

  EXPECT_EQ("binary2", entries_[1].binary);
  EXPECT_EQ("local_name2", entries_[1].local_name);
  EXPECT_EQ("com.google.fuchsia.pick.v1", entries_[1].verb);
  EXPECT_EQ(1lu, entries_[1].noun_constraints.size());
  EXPECT_EQ("thing", entries_[1].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries_[1].noun_constraints[0].types.size());
  EXPECT_EQ("frob", entries_[1].noun_constraints[0].types[0]);

  // Add a new file, expect to see the results.
  WriteManifestFile("manifest3", kManifest3);
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 3; }));

  EXPECT_EQ("binary3", entries_[2].binary);
  EXPECT_EQ("local_name3", entries_[2].local_name);
  EXPECT_EQ("com.google.fuchsia.annotate.v1", entries_[2].verb);
  EXPECT_EQ(1lu, entries_[2].noun_constraints.size());
  EXPECT_EQ("thingy", entries_[2].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries_[2].noun_constraints[0].types.size());
  EXPECT_EQ("chair", entries_[2].noun_constraints[0].types[0]);
}

TEST_F(DirectoryModuleManifestSourceTest, RemovedFiles) {
  // Write a manifest file before creating the repo.
  WriteManifestFile("manifest1", kManifest1);
  WriteManifestFile("manifest2", kManifest2);

  Reset();
  ASSERT_TRUE(RunLoopUntil([this]() { return idle_; }));

  RemoveManifestFile("manifest1");
  RemoveManifestFile("manifest2");
  ASSERT_TRUE(RunLoopUntil([this]() { return removed_ids_.size() == 2; }));
  EXPECT_EQ(removed_ids_[0], entry_ids_[0]);
  EXPECT_EQ(removed_ids_[1], entry_ids_[1]);
}

TEST_F(DirectoryModuleManifestSourceTest, RepoDirIsCreatedAutomatically) {
  repo_dir_ = "/tmp/foo";
  // TODO(thatguy): Once making DirectoryRepository easier to test against
  // (ie, have a guaranteed initialized state we can synchronize on), do that
  // here.
  Reset(true /* create_dir */);
  WriteManifestFile("manifest3", kManifest3);
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 1; }));
}

TEST_F(DirectoryModuleManifestSourceTest, IgnoreIncomingFiles) {
  Reset();
  WriteManifestFile("foo.incoming", kManifest2);
  WriteManifestFile("foo", kManifest3);

  // The first manifest files contains two entries, but we should ignore them.
  ASSERT_TRUE(RunLoopUntil([this]() { return entries_.size() == 1; }));
}

}  // namespace
}  // namespace modular
