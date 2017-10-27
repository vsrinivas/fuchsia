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
    repo_.reset(new ModuleManifestRepository(
        repo_dir_, [this](ModuleManifestRepository::Entry entry) {
          entries_.push_back(entry);
        }));
  }

  void WriteManifestFile(const std::string& name,
                         const char* contents,
                         int len) {
    const auto path = repo_dir_ + '/' + name;
    manifests_written_.push_back(path);

    FXL_CHECK(files::WriteFile(path, contents, len));
  }

  std::vector<ModuleManifestRepository::Entry> entries() const {
    return entries_;
  }

  std::vector<std::string> manifests_written_;
  std::string repo_dir_;
  std::unique_ptr<ModuleManifestRepository> repo_;

  std::vector<ModuleManifestRepository::Entry> entries_;
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

TEST_F(ModuleManifestRepositoryTest, All) {
  // Write a manifest file before creating the repo.
  WriteManifestFile("manifest1", kManifest1, strlen(kManifest1));

  ResetRepository();

  ASSERT_TRUE(RunLoopUntil([this]() { return entries().size() == 2; }));
  EXPECT_EQ("m1_binary1", entries()[0].binary);
  EXPECT_EQ("m1_local_name1", entries()[0].local_name);
  EXPECT_EQ("com.google.fuchsia.navigate.v1", entries()[0].verb);
  EXPECT_EQ(2lu, entries()[0].noun_constraints.size());
  EXPECT_EQ("start", entries()[0].noun_constraints[0].name);
  EXPECT_EQ(2lu, entries()[0].noun_constraints[0].types.size());
  EXPECT_EQ("foo", entries()[0].noun_constraints[0].types[0]);
  EXPECT_EQ("bar", entries()[0].noun_constraints[0].types[1]);
  EXPECT_EQ("destination", entries()[0].noun_constraints[1].name);
  EXPECT_EQ(1lu, entries()[0].noun_constraints[1].types.size());
  EXPECT_EQ("baz", entries()[0].noun_constraints[1].types[0]);

  EXPECT_EQ("m1_binary2", entries()[1].binary);
  EXPECT_EQ("m1_local_name2", entries()[1].local_name);
  EXPECT_EQ("com.google.fuchsia.pick.v1", entries()[1].verb);
  EXPECT_EQ(1lu, entries()[1].noun_constraints.size());
  EXPECT_EQ("thing", entries()[1].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries()[1].noun_constraints[0].types.size());
  EXPECT_EQ("frob", entries()[1].noun_constraints[0].types[0]);

  // Add a new file, expect to see the results.
  WriteManifestFile("manifest2", kManifest2, strlen(kManifest2));
  ASSERT_TRUE(RunLoopUntil([this]() { return entries().size() == 3; }));

  EXPECT_EQ("binary", entries()[2].binary);
  EXPECT_EQ("local_name", entries()[2].local_name);
  EXPECT_EQ("Annotate", entries()[2].verb);
  EXPECT_EQ(1lu, entries()[2].noun_constraints.size());
  EXPECT_EQ("thingy", entries()[2].noun_constraints[0].name);
  EXPECT_EQ(1lu, entries()[2].noun_constraints[0].types.size());
  EXPECT_EQ("chair", entries()[2].noun_constraints[0].types[0]);
}

TEST_F(ModuleManifestRepositoryTest, DontCrashOnNoRepoDir) {
  repo_dir_ = "/doesnt/exist";
  // TODO(thatguy): Once making ModuleManifestRepository easier to test against
  // (ie, have a guaranteed initialized state we can synchronize on), do that
  // here.
  ResetRepository();
}

}  // namespace
}  // namespace modular
